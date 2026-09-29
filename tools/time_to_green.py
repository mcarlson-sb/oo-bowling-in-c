#!/usr/bin/env python3
"""How long the integration branch was red before this run turned it green.

Reads the gate workflow's earlier completed runs on the branch from the Actions API, newest
first. Every run that didn't succeed since the last successful one is part of the red spell; the
spell began when the first of them started. Prints one Markdown line for the job summary.

Usage: time_to_green.py <owner/repo> <workflow file> <branch> <this run's id>
The token, if any, is read from GITHUB_TOKEN (the repository is public, so reads work without).
TIME_TO_GREEN_API overrides the API's base URL (for proving the failure path).

It never fails: it runs in the promote job, where nothing may turn a promotion red. Any error
reading the API prints "time to green unavailable" and exits 0.
"""

import datetime
import json
import os
import sys
import urllib.parse
import urllib.request


def get(url):
    request = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"})
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        request.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(request) as response:
        return json.load(response)


def parse(stamp):
    return datetime.datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ").replace(
        tzinfo=datetime.timezone.utc)


def minutes_and_seconds(delta):
    seconds = int(delta.total_seconds())
    return "%dm %02ds" % (seconds // 60, seconds % 60)


def red_spell(runs, this_run):
    """The runs since the last green one, oldest last, or [] if the one before was green."""
    red = []
    for run in runs:
        if run["id"] == this_run or run["status"] != "completed":
            continue
        if run["conclusion"] == "success":
            break
        red.append(run)
    return red


def report(repo, workflow, branch, this_run):
    base = os.environ.get("TIME_TO_GREEN_API", "https://api.github.com")
    url = "%s/repos/%s/actions/workflows/%s/runs?branch=%s&per_page=50" % (
        base, repo, workflow, urllib.parse.quote(branch, safe=""))
    runs = get(url)["workflow_runs"]
    red = red_spell(runs, this_run)
    if not red:
        print("**Time to green:** the run before this one was green; the line was never red.")
        return 0
    began = parse(red[-1]["run_started_at"])
    now = datetime.datetime.now(datetime.timezone.utc)
    print("**Time to green:** integration was red for **%s**, over %d red run%s, since %s ([the "
          "first red run](%s))." % (minutes_and_seconds(now - began), len(red),
                                     "" if len(red) == 1 else "s",
                                     red[-1]["run_started_at"], red[-1]["html_url"]))
    return 0


def main(argv):
    try:
        repo, workflow, branch, this_run = argv[0], argv[1], argv[2], int(argv[3])
        return report(repo, workflow, branch, this_run)
    except Exception as error:  # pylint: disable=broad-except -- it must not fail the job
        print("**Time to green:** time to green unavailable (%s: %s)."
              % (type(error).__name__, error))
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
