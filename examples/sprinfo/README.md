# sprinfo Command

## Overview

The `sprinfo` command is a utility for retrieving version information of Spresense. When executed, it outputs the Spresense firmware version and bootloader version in JSON format.

## Usage

```bash
sprinfo
```

When executed without arguments, it outputs version information in JSON format.

## Output Example

```json
{"status":{"code":0,"msg":"Success"},"data":{"spresense":"2023-06-05","bootloader":"v3.4.1"}}
```

## Output Format

The output is returned in the following JSON format:

```json
{
  "status": {
    "code": 0,
    "msg": "Success"
  },
  "data": {
    "spresense": "<Spresense version>",
    "bootloader": "<Bootloader version>"
  }
}
```

### Status Codes

- `0`: Success
- `-1`: Error (e.g., when an unknown option is specified)

## Error Output Example

When an unknown option is specified:

```json
{"status":{"code":-1,"msg":"Unknown option: --help"}}
```

## Technical Details

- `SPRESENSE_VERSION`: Automatically generated from the latest commit date of the master branch
- `BOOTLOADER_VERSION`: Retrieved from the `version.json` file

## File Structure

- `sprinfo_main.c`: Main source code
- `spresense_version.h`: Header file defining version information (automatically generated during build)
- `generate_version.py`: Python script to generate the version header

## Notes

- Version information is automatically generated at build time
- For accurate version information, build from the latest source code

## Focus update

spresense_version.h is created only once initially, but it does not update automatically.
To force an update, please delete it first:

```sh
cd sdk/include
rm spresense_version.h
```
