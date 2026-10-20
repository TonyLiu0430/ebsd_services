"""Create the local .env file with safe defaults and random secrets."""

import os
import secrets
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
TEMPLATE_PATH = PROJECT_ROOT / ".env.example"
ENV_PATH = PROJECT_ROOT / ".env"

GENERATED_VALUES = {
    "POSTGRES_DB": "ebsd",
    "POSTGRES_USER": "ebsd",
    "POSTGRES_PASSWORD": secrets.token_hex(32),
    "JWT_SECRET": secrets.token_hex(32),
}


def fill_template(template: str) -> str:
    lines = template.splitlines()
    found_names = set()

    for index, line in enumerate(lines):
        if "=" not in line or line.lstrip().startswith("#"):
            continue
        name, value = line.split("=", 1)
        found_names.add(name)
        if name in GENERATED_VALUES and value == "":
            lines[index] = f"{name}={GENERATED_VALUES[name]}"

    for name, value in GENERATED_VALUES.items():
        if name not in found_names:
            lines.append(f"{name}={value}")

    return "\n".join(lines) + "\n"


def main() -> None:
    template = TEMPLATE_PATH.read_text(encoding="utf-8")
    contents = fill_template(template)

    try:
        file_descriptor = os.open(
            ENV_PATH,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL,
            0o600,
        )
    except FileExistsError:
        print(".env 已存在，保留原有設定。")
        return

    with os.fdopen(file_descriptor, "w", encoding="utf-8", newline="\n") as env_file:
        env_file.write(contents)

    print("已建立 .env，密碼與金鑰已自動產生。")


if __name__ == "__main__":
    main()
