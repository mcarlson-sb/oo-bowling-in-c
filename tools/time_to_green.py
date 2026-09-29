#!/usr/bin/env python3
"""How long the integration branch was red before this run turned it green.

Reads the gate workflow's earlier completed runs on the branch from the Actions API, newest
first. Every run that didn't succeed since the last successful one is part of the red spell; the
spell began when the first of them started. Prints one Markdown line for the job summary.

Usage: time_to_green.py <owner/repo> <workflow file> <branch> <this run's id>
The token, if any, is read from GITHUB_TOKEN (the repository is public, so reads work without).
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


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    repo, workflow, branch, this_run = argv[0], argv[1], argv[2], int(argv[3])
    url = "https://api.github.com/repos/%s/actions/workflows/%s/runs?branch=%s&per_page=50" % (
        repo, workflow, urllib.parse.quote(branch, safe=""))
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


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
