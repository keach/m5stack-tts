Import("env")

import datetime
import subprocess


def git_output(*args):
    try:
        return subprocess.check_output(
            ["git", *args],
            cwd=env.subst("$PROJECT_DIR"),
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def cpp_string(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return '\\"{}\\"'.format(escaped)


short_commit = git_output("rev-parse", "--short=7", "HEAD")
nearest_tag = git_output("describe", "--tags", "--abbrev=0")
status = git_output("status", "--porcelain", "--untracked-files=normal")

if short_commit:
    firmware_version = "{}+{}".format(nearest_tag or "untagged", short_commit)
    if status:
        firmware_version += "-dirty"
else:
    firmware_version = "unknown"

commit_date = "unknown"
commit_timestamp = git_output("show", "-s", "--format=%cI", "HEAD")
if commit_timestamp:
    try:
        parsed = datetime.datetime.fromisoformat(commit_timestamp)
        jst = datetime.timezone(datetime.timedelta(hours=9))
        commit_date = parsed.astimezone(jst).strftime("%Y-%m-%d %H:%M JST")
    except ValueError:
        pass

env.Append(
    CPPDEFINES=[
        ("FIRMWARE_VERSION", cpp_string(firmware_version)),
        ("FIRMWARE_COMMIT_DATE", cpp_string(commit_date)),
    ]
)

print("Firmware version: {}".format(firmware_version))
print("Firmware commit date: {}".format(commit_date))
