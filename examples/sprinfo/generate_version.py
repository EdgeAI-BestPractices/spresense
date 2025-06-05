#!/usr/bin/env python3
import json
import sys
import subprocess
import os

def get_git_master_date():
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%cd", "--date=format:%Y-%m-%d", "master"],
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip()
    except Exception as e:
        print(f"Warning: Failed to get git date: {e}")
        return "unknown"

def main():
    if len(sys.argv) != 3:
        print("Usage: generate_version.py <version.json> <output_header>")
        sys.exit(1)

    json_file = sys.argv[1]
    header_file = sys.argv[2]

    try:
        with open(json_file, 'r') as f:
            version_data = json.load(f)

        bootloader_version = version_data.get('LoaderVersion', 'unknown')
        spresense_version = get_git_master_date()

        header_content = f'''#ifndef __SPRESENSE_VERSION_H
#define __SPRESENSE_VERSION_H

#define SPRESENSE_VERSION "{spresense_version}"

#define BOOTLOADER_VERSION "{bootloader_version}"

#endif /* __SPRESENSE_VERSION_H */
'''

        with open(header_file, 'w') as f:
            f.write(header_content)

        print(f"Generated {header_file} with version {bootloader_version}")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
