"""Update the source checkout and rebuild its Docker Compose services."""

import argparse
import subprocess
import sys
from pathlib import Path
from typing import List


PROJECT_ROOT = Path(__file__).resolve().parent.parent
OPTIONAL_PROFILES = {
    "avahi": "mdns",
    "cloudflared": "tunnel",
}


def running_profiles() -> List[str]:
    result = subprocess.run(
        [
            "docker", "compose",
            "--profile", "mdns",
            "--profile", "tunnel",
            "ps", "--services", "--status", "running",
        ],
        cwd=PROJECT_ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    running_services = set(result.stdout.splitlines())
    return [
        profile
        for service, profile in OPTIONAL_PROFILES.items()
        if service in running_services
    ]


def current_revision() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=PROJECT_ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    return result.stdout.strip()


def update(force: bool = False) -> None:
    revision_before = current_revision()
    print("正在更新原始碼……", flush=True)
    subprocess.run(
        ["git", "pull", "--ff-only"],
        cwd=PROJECT_ROOT,
        check=True,
    )
    revision_after = current_revision()

    if revision_before == revision_after and not force:
        print("原始碼已是最新版，不需重新建置或重啟服務。")
        return

    profiles = running_profiles()
    compose_command = ["docker", "compose"]
    for profile in profiles:
        compose_command.extend(["--profile", profile])
    compose_command.extend(["up", "-d", "--build"])

    print("正在重建並更新服務……", flush=True)
    subprocess.run(compose_command, cwd=PROJECT_ROOT, check=True)
    print("更新完成。")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="更新原始碼並重新建置服務。")
    parser.add_argument(
        "--force",
        action="store_true",
        help="即使沒有新提交也重新建置服務。",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        update(force=args.force)
    except FileNotFoundError as error:
        print(f"找不到必要指令：{error.filename}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        if error.stderr:
            print(error.stderr.strip(), file=sys.stderr)
        print(f"更新停止：指令執行失敗（結束碼 {error.returncode}）。", file=sys.stderr)
        return error.returncode or 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
