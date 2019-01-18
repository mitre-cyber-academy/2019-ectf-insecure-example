#!/usr/bin/python3

import os
import subprocess
import re
import argparse

# Path to the mesh_users header file
mesh_users_fn = os.environ["ECTF_UBOOT"] + "/include/mesh_users.h"
# Path to the default_games header file
default_games_hpath = os.environ["ECTF_UBOOT"] + "/include/default_games.h"
# Path where generated files will go
gen_path = "files/generated"
# File name for the bif file
system_image_fn = "SystemImage.bif"
# File name for the factory secrets
factory_secrets_fn = "FactorySecrets.txt"


def validate_users(lines):
    """Validate that the users data is formatted properly and return a list
    of tuples of users and pins.

    lines: list of strings from a users.txt file with newlines removed
    """
    # Regular expression to ensure that there is a username and an 8 digit pin
    reg = r'^\s*(\w+)\s+(\d{8})\s*$'
    lines = [(m.group(1), m.group(2)) for line in lines
             for m in [re.match(reg, line)] if m]

    # return a list of tuples of (username, pin)
    return lines


def write_mesh_users_h(users, f):
    """Write user inforation to a header file

    users: list of tuples of (username, pin)
    f: open file object for the header file to be written
    """
    # write users to header file
    f.write("""
/*
* This is an automatically generated file by provisionSystem.py
*
*
*/

#ifndef __MESH_USERS_H__
#define __MESH_USERS_H__

#define NUM_MESH_USERS {num_users}

struct MeshUser {{
    char username[16];
    char pin[9];
}};

static struct MeshUser mesh_users[] = {{
""".format(num_users=len(users)))

    for (user, pin) in users:
        data = '    {.username="%s", .pin="%s"},\n' % (user, pin)
        f.write(data)

    f.write("""
};

#endif /* __MESH_USERS_H__ */
""")


def write_mesh_default_h(default_txt_path, header_path):
    """Turn the default.txt into a C header file for MESH

    default_txt_path: path to the default.txt file to be read from
    header_path: path to the C header file to be written
    """

    # Open the file and read into a variable
    with open(default_txt_path, 'r') as f:
        lines = f.read().split('\n')

    # Base string to write
    s = """
/*
* This is an automatically generated file by provisionSystem.py
*
*
*/

#ifndef __MESH_DEFAULT_TXT_H__
#define __MESH_DEFAULT_TXT_H__

"""

    # Write the number of default games
    s += "#define NUM_DEFAULT_GAMES %s\n" % sum(1 for line in lines if line)
    # For each line, write the game information
    s += """
static char* default_games[] = {
"""
    for line in lines:
        # Ignore blank lines
        if not line:
            continue
        # Split on space
        line = line.split()
        # Game name is before the first space
        game_name = line[0]
        # Split what's after the first space (version information) into
        # major.minor
        line = line[1].split('.')
        major = line[0]
        minor = line[1]
        # Write the information as gamename-vmajor.minor to the header file
        s += "    \"%s-v%s.%s\",\n" % (game_name, major, minor)

    s += """
};

#endif /* __MESH_DEFAULT_TXT_H__ */
"""
    with open(header_path, 'w') as f:
        f.write(s)


def build_images():
    """Create MES.bin using the petalinux tools"""
    print("Building Images... this may take a while!")
    # Source the petalinux env, then cd into the source code directory.
    # Clean the project (since petalinux doesn't always build correctly
    # depending on what files you have modified; for example configs)
    # then build everything
    subprocess.check_call(["/bin/bash", "-i", "-c", "petalinuxenv > /dev/null && cd $ECTF_PETALINUX/Arty-Z7-10/ && petalinux-build -x distclean && petalinux-build"])
    print("Done Building Images to %s" % (os.environ["ECTF_PETALINUX"] + '/Arty-Z7-10/images'))


def write_system_image_bif(f):
    """Write the bif file

    f: open file to write the bif to
    """
    f.write("""
MITRE_Entertainment_System: {{
    [bootloader] /home/vagrant/MES/tools/files/zynq_fsbl.elf
    // Participants Bitstream
    {path}/Arty-Z7-10/images/linux/Arty_Z7_10_wrapper.bit
    // Paritcipants Images
    {path}/Arty-Z7-10/images/linux/u-boot.elf
    [load=0x10000000] {path}/Arty-Z7-10/images/linux/image.ub
}}
    """.format(path=os.environ["ECTF_PETALINUX"]))


def write_factory_secrets(f):
    """Write any factory secrets. The reference implementation has none

    f: open file to write the factory secrets to
    """
    None


def main():
    # Argument parsing
    parser = argparse.ArgumentParser()
    parser.add_argument('USERS_FILE', help="""
    This a text file that includes username and passwords in a MITRE
    defined format

    # comment
    User1 12345678
    User2 12345678
        """)
    parser.add_argument('DEFAULT_FILE', help="""
    This a text file that includes game name and version of games
    that must be installed by defalut in order for the system to boot.

    # comment
    game_foo 1.1
    game_bar 2.0
        """)
    args = parser.parse_args()

    # open arg file
    try:
        f_mesh_users_in = open(args.USERS_FILE, "r")
    except Exception as e:
        print("Unable to open users text file %s: %s" % (args.USERS_FILE, e,))
        exit(2)

    # Create the folder where the generated files will go
    # (any any parent folders)
    subprocess.check_call("mkdir -p " + gen_path, shell=True)
    # Try to open each file that we'll need to write to, report error messages
    try:
        f_mesh_users_out = open(mesh_users_fn, "w+")
    except Exception as e:
        print("Unable to open generated users header file: %s" % (e,))
        exit(2)
    try:
        f_system_image = open(os.path.join(gen_path, system_image_fn), "w+")
    except Exception as e:
        print("Unable to open %s: %s" % (system_image_fn, e,))
        exit(2)
    try:
        f_factory_secrets = open(os.path.join(gen_path, factory_secrets_fn), "w+")
    except Exception as e:
        print("Unable to open %s: %s" % (factory_secrets_fn, e,))
        exit(2)

    # Read in all of the user information into a list and strip newlines
    lines = [line.rstrip('\n') for line in f_mesh_users_in]

    # parse user strings
    try:
        users = validate_users(lines)
    except Exception as e:
            print("Users text file is misformated.")
            exit(2)

    # Add the demo user, which must always exist, per the rules
    users.append(("demo", "00000000"))
    # write mesh users to uboot header
    write_mesh_users_h(users, f_mesh_users_out)
    f_mesh_users_out.close()
    print("Generated mesh_users.h file: %s" % (mesh_users_fn))

    # Write the default games file
    write_mesh_default_h(args.DEFAULT_FILE, default_games_hpath)
    print("Generated default_games.h file")

    # build MES.bin
    build_images()

    # write system image bif
    write_system_image_bif(f_system_image)
    f_system_image.close()
    print("Generated SystemImage file: %s" % (os.path.join(gen_path, system_image_fn)))

    # write factory secrets
    write_factory_secrets(f_factory_secrets)
    f_factory_secrets.close()
    print("Generated FactorySecrets file: %s" % (os.path.join(gen_path, factory_secrets_fn)))

    exit(0)


if __name__ == '__main__':
    main()
