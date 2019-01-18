#include <common.h>
#include <cli.h>
#include <stdlib.h>
#include <ext_common.h>
#include <ext4fs.h>
#include "../fs/ext4/ext4_common.h"
#include <fs.h>
#include <spi.h>
#include <spi_flash.h>
#include <command.h>
#include <os.h>

#include <mesh.h>
#include <mesh_users.h>
#include <default_games.h>

#define MESH_TOK_BUFSIZE 64
#define MESH_TOK_DELIM " \t\r\n\a"
#define MESH_RL_BUFSIZE 1024
#define MESH_SHUTDOWN -2

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

// declare user global
User user;

/*
    List of builtin commands, followed by their corresponding functions.
 */
char *builtin_str[] = {
    "help",
    "shutdown",
    "logout",
    "list",
    "play",
    "query",
    "install",
    "uninstall",
    "dump",
    "resetflash"
};

int (*builtin_func[]) (char **) = {
    &mesh_help,
    &mesh_shutdown,
    &mesh_logout,
    &mesh_list,
    &mesh_play,
    &mesh_query,
    &mesh_install,
    &mesh_uninstall,
    &mesh_dump_flash,
    &mesh_reset_flash
};


/******************************************************************************/
/********************************** Flash Commands ****************************/
/******************************************************************************/

/*
    This function initializes the game install table. If the the sentinel is 
    written already, then, it does nothing, otherwise, it writes the sentinel 
    and the MESH_TABLE_END flag to the beginning of the game install table.
*/
int mesh_init_table(void)
{
    /* Initialize the table where games will be installed */
    char* sentinel = (char*) malloc(sizeof(char) * MESH_SENTINEL_LENGTH);
    int ret = 1;

    mesh_flash_read(sentinel, MESH_SENTINEL_LOCATION, MESH_SENTINEL_LENGTH);
    if (*((unsigned int*) sentinel) != MESH_SENTINEL_VALUE)
    {
        unsigned int sentinel_value = MESH_SENTINEL_VALUE;
        mesh_flash_write(&sentinel_value, MESH_SENTINEL_LOCATION, MESH_SENTINEL_LENGTH);
        unsigned int tend = MESH_TABLE_END;

        // write table end
        mesh_flash_write(&tend, MESH_INSTALL_GAME_OFFSET, sizeof(char));
        ret = 0;
    }
    free(sentinel);
    return ret;
}

/*
    This function initialized the flash memory for the Arty Z7. This must be done
    before executing any flash memory commands.
*/
int mesh_flash_init(void)
{
    char* probe_cmd[] = {"sf", "probe", "0", "2000000", "0"};
    cmd_tbl_t* sf_tp = find_cmd("sf");
    return sf_tp->cmd(sf_tp, 0, 5, probe_cmd);
}

/*
    This is an improved version of the u-boot sf write. It allows you to update
    the flash not on the page bounderies. Normally, the flash write can only
    toggle 1's to 0's and erase can only reset the flash to 1's on page boundaries
    and in chunks of a single page.

    This is a wrapper that reads a page, updates the necessary bits, and then 
    updates the entire page in flash.

    It writes the byte array data of length flash_length to flash address at 
    flash_location.
*/

int mesh_flash_write(void* data, unsigned int flash_location, unsigned int flash_length)
{
    /* Write flash_length number of bytes starting at what's pointed to by data
     * to address flash_location in flash.
     */

    if (flash_length < 1)
        return 0;

    // We use the "sf update" command to update flash. Under the hood, this
    // calls out to "sf erase" and "sf write". The "sf erase" command needs
    // to be called on erase page boundaries (size 64 KB), so we need to make
    // sure that we only call "sf update" on those boundaries as well.
    // Since we want to write data to arbitrary locations in flash
    // (potentially across page boundaries) we need to break our data up so
    // that we can write to said boundaries.
    //
    // To do so, we read in the whole page that we're going to write to into RAM,
    // update (in RAM) the data, and then write the page back out.

    // Determine the starting and ending pages so that we know how many pages
    // we need to write to
    unsigned int starting_page = flash_location / FLASH_PAGE_SIZE;
    unsigned int ending_page = (flash_location + flash_length) / FLASH_PAGE_SIZE;

    // malloc space to hold an entire page
    char* flash_data = malloc(sizeof(char) * FLASH_PAGE_SIZE);

    // Find the sf sub command, defined by u-boot
    cmd_tbl_t* sf_tp = find_cmd("sf");

    // The number of bytes that we've copied to flash so far
    // This is so that we know when we've copied flash_length
    // number of bytes
    unsigned int bytes_copied = 0;

    // Loop over all of the pages that our data would touch and
    // write the modified pages
    for(unsigned int i = starting_page; i <= ending_page; ++i)
    {
        // Get the address (in flash) of the page we need to write
        unsigned int page_starting_address = i * FLASH_PAGE_SIZE;
        // read all of the page data into a buffer
        mesh_flash_read(flash_data, page_starting_address, FLASH_PAGE_SIZE);

        // If this is the first page, we need to stop on the page boundary
        // or once we've written the correct number of bytes specified by
        // flash_length
        if (i == starting_page)
        {
            // Copy (byte by byte) until we've either reached the end of
            // this page, or we've copied the appropriate number of bytes
            for (;
                 (flash_location + bytes_copied < page_starting_address + FLASH_PAGE_SIZE) && (bytes_copied < flash_length);
                 ++bytes_copied)
            {
                flash_data[(flash_location % FLASH_PAGE_SIZE) + bytes_copied] = ((char*) data)[bytes_copied];
            }
        }
        // Otherwise, we either have an entire page that needs to be updated,
        // or a partial page that we need to update. Either way, this page
        // starts on a page bound
        else
        {
            // Copy (byte by byte) until we've either reached the end of
            // this page, or we've copied the appropriate number of bytes
            for (unsigned int j = 0;
                 (i * FLASH_PAGE_SIZE + j < (i + 1) * FLASH_PAGE_SIZE) && (bytes_copied < flash_length);
                 ++j)
            {
                flash_data[j] = ((char*) data)[bytes_copied];
                ++bytes_copied;
            }
        }

        // We need to convert things to strings since this mimics the command prompt
        char data_ptr_str[11] = "";
        char offset_str[11] = "";
        char length_str[11] = "";

        // Convert the pointer to a string representation (0xffffffff)
        ptr_to_string(flash_data, data_ptr_str);
        ptr_to_string((void *) page_starting_address, offset_str);
        ptr_to_string((void *) FLASH_PAGE_SIZE, length_str);

        // Perform an update on this page
        char* write_cmd[] = {"sf", "update", data_ptr_str, offset_str, length_str};
        sf_tp->cmd(sf_tp, 0, 5, write_cmd);
    }

    free(flash_data);

    return 0;
}

/*
    This function reads flash_length bytes from the flash memory at flash_location
    to the byte array data.
*/
int mesh_flash_read(void* data, unsigned int flash_location, unsigned int flash_length)
{
    /* Read "flash_length" number of bytes from "flash_location" into "data" */

    // Find the sf sub command
    cmd_tbl_t* sf_tp = find_cmd("sf");

    // We need to convert things to strings since this mimics the command prompt,
    // so get us space for strings
    char str_ptr[11] = "";
    char offset_ptr[11] = "";
    char length_ptr[11] = "";
    // Convert the point to a string representation
    ptr_to_string(data, str_ptr);
    ptr_to_string((unsigned int *) flash_location, offset_ptr);
    ptr_to_string((unsigned int *) flash_length, length_ptr);

    // Perform an update
    char* read_cmd[] = {"sf", "read", str_ptr, offset_ptr, length_ptr};
    return sf_tp->cmd(sf_tp, 0, 5, read_cmd);
}

/******************************************************************************/
/******************************** End Flash Commands **************************/
/******************************************************************************/

/******************************************************************************/
/********************************** MESH Commands *****************************/
/******************************************************************************/

/* 
    This function lists all commands available from the mesh shell. It
    implements the help function in the mesh shell. 
*/
int mesh_help(char **args)
{
    /* List all valid commands */
    int i;
    printf("Welcome to the MITRE entertainment system\n");
    printf("The commands available to you are listed below:\n");

    for (i = 0; i < mesh_num_builtins(); i++)
    {
        printf("  %s\n", builtin_str[i]);
    }

    return 0;
}

/* 
    This shuts down the mesh terminal. It does not shut down the board.
    This implements the shutdown function in the mesh shell 
*/
int mesh_shutdown(char **args)
{
    /* Exit the shell completely */
    memset(user.name, 0, MAX_STR_LEN);
    return MESH_SHUTDOWN;
}

/* 
   Log the current user out of mesh. The control loop brings the user
   back to the login prompt. This implements the logout function in the mesh
   shell.
*/
int mesh_logout(char **args)
{
    /* Exit the shell, allow other user to login */
    memset(user.name, 0, MAX_STR_LEN);
    return 0;
}

/*
    List all installed games for the given user. This implements the list
    function in the mesh shell.
*/
int mesh_list(char **args)
{
    struct games_tbl_row row;
    unsigned int offset = MESH_INSTALL_GAME_OFFSET;

    // loop through install table untill end of table is found.
    for(mesh_flash_read(&row, offset, sizeof(struct games_tbl_row));
        row.install_flag != MESH_TABLE_END;
        mesh_flash_read(&row, offset, sizeof(struct games_tbl_row)))
    {
        // print the game if it is found.
        if (strcmp(row.user_name, user.name) == 0 && row.install_flag == MESH_TABLE_INSTALLED)
            printf("%s-v%d.%d\n", row.game_name, row.major_version, row.minor_version);
        offset += sizeof(struct games_tbl_row);
    }

    return 0;
}

/*
    This function writes the specified game to ram address 0x1fc00040 and the
    size of the specified game binary to 0x1fc00000. It then boots the linux
    kernel from ram address 0x10000000. This allows the linux kernel to read the
    binary and execute it to play the game..

    This function implements the play function in mesh. 
*/
int mesh_play(char **args)
{
    if (!mesh_play_validate_args(args)){
        return 0;
    }

    Game game;
    mesh_get_game_header(&game, args[1]);

    if (mesh_check_downgrade(args[1], game.major_version, game.minor_version) == 1){
        printf("You are not allowed to play an older version of the game once a newer one is installed.\n");
        return 0;
    }

    loff_t size = 0;

    // get size of binary
    size = mesh_size_ext4(args[1]);

    // write game size to memory
    char *size_str = (char *)malloc(sizeof(int));
    sprintf(size_str, "0x%x", (int) size);
    char * const mw_argv[3] = { "mw.l", "0x1fc00000", size_str };
    cmd_tbl_t* mem_write_tp = find_cmd("mw.l");
    mem_write_tp->cmd(mem_write_tp, 0, 3, mw_argv);

    // load game binary into memory
    char * const argv[5] = { "ext4load", "mmc", "0:2", "0x1fc00040", args[1] };
    cmd_tbl_t* load_tp = find_cmd("ext4load");

    load_tp->cmd(load_tp, 0, 5, argv);

    // cleanup - this is here because boot may not execute following commands
    free(size_str);

    // boot petalinux
    char * const boot_argv[2] = { "bootm", "0x10000000"};
    cmd_tbl_t* boot_tp = find_cmd("bootm");
    boot_tp->cmd(boot_tp, 0, 2, boot_argv);

    return 0;
}

/*
    This function lists all games that are installed for the specified user.
    It implements the mesh shell query function.
*/
int mesh_query(char **args)
{
    /* List all games available to download */
    printf("%s's games...\n", user.name);
    return mesh_query_ext4("/", NULL) < 0 ? 0 : 1;
}


/*
    This function installs the given game for the specified user. 
    It finds the next available spot in the install table.

    It implements the install function of the mesh shell.
*/
int mesh_install(char **args)
{
    /* Install the game */

    int validated = 0;
    if ((validated = mesh_install_validate_args(args))){
        return validated;
    }

    char* full_game_name = args[1];

    // get the short name of the game (the stuff before the "-")
    char* short_game_name = strtok(full_game_name, "-");

    // get the major and minor version of the game
    char* major_version = strtok(NULL, ".") + 1;  // +1 becase of the "v"
    char* minor_version = strtok(NULL, "\0");

    // Row for this game
    struct games_tbl_row row;
    // Flag saying that this game is installed
    row.install_flag = MESH_TABLE_INSTALLED;

    // Copy the game name into our struct (padded with 0's)
    int i;
    for(i = 0; i < MAX_GAME_LENGTH && short_game_name[i] != '\0'; ++i)
        row.game_name[i] = short_game_name[i];
    for(; i < MAX_GAME_LENGTH; ++i)
        row.game_name[i] = 0;
    row.game_name[MAX_GAME_LENGTH] = 0;

    // copy the username into the struct (padded with 0's)
    for(i = 0; i <= MAX_USERNAME_LENGTH && user.name[i] != '\0'; ++i)
        row.user_name[i] = user.name[i];
    for(; i <= MAX_USERNAME_LENGTH; ++i)
        row.user_name[i] = 0;
    row.user_name[MAX_USERNAME_LENGTH] = 0;

    row.major_version = simple_strtoul(major_version, NULL, 10);
    row.minor_version = simple_strtoul(minor_version, NULL, 10);

    printf("Installing game %s for %s...\n", row.game_name, row.user_name);


    // Get the initial offset into the games table
    unsigned int offset = MESH_INSTALL_GAME_OFFSET;
    // Flag for if this row is in use

    struct games_tbl_row flash_row;
    // Find the end of the table
    for(mesh_flash_read(&flash_row, offset, sizeof(struct games_tbl_row));
        flash_row.install_flag != MESH_TABLE_END;
        mesh_flash_read(&flash_row, offset, sizeof(struct games_tbl_row)))
    {
        offset += sizeof(struct games_tbl_row);
    }

    // Write this row at the specified offset
    mesh_flash_write(&row, offset, sizeof(struct games_tbl_row));
    // Now we need to potentially signal the end of the table
    // I say potentially because it's possible that we wrote over a game
    // that was uninstalled, in which case we don't need to write the end of
    // the table since we can assume that it's already there

    // Increase the offset to past this row
    offset += sizeof(struct games_tbl_row);

    // Write the end of the table
    char end = MESH_TABLE_END;
    mesh_flash_write(&end, offset, sizeof(char));

    printf("%s was successfully installed for %s\n", row.game_name, row.user_name);
    return 0;
}


/*
    This function uninstalls the specified game for the given user.
    This function implements the uninstall function of the mesh shell.
*/
int mesh_uninstall(char **args)
{
    /* Remove the game for this user*/
    /* List all of the installed games for this user */

    if (!mesh_game_installed(args[1])) {
        printf("%s is not installed for %s.\n", args[1], user.name);
        return 0;
    }

    struct games_tbl_row row;
    unsigned int offset = MESH_INSTALL_GAME_OFFSET;

    printf("Uninstalling %s for %s...\n", args[1], user.name);
    for(mesh_flash_read(&row, offset, sizeof(struct games_tbl_row));
        row.install_flag != MESH_TABLE_END;
        mesh_flash_read(&row, offset, sizeof(struct games_tbl_row)))
    {
        // the most space that we could need to store the full game name
        char* full_name = (char*) malloc(snprintf(NULL, 0, "%s-v%d.%d", row.game_name, row.major_version, row.minor_version) + 1);
        full_name_from_short_name(full_name, &row);

        if (strcmp(row.user_name, user.name) == 0 &&
            strcmp(full_name, args[1]) == 0 &&
            row.install_flag == MESH_TABLE_INSTALLED)
        {
            row.install_flag = MESH_TABLE_UNINSTALLED;
            mesh_flash_write(&row, offset, sizeof(struct games_tbl_row));
            printf("%s was successfully uninstalled for %s\n", args[1], user.name);
            free(full_name);
            break;
        }
        free(full_name);
        offset += sizeof(struct games_tbl_row);
    }

    return 0;
}


/* 
    This is a development utility that allows you to easily dump flash
    memory to std out.
*/
int mesh_dump_flash(char **args)
{
    int argv = mesh_get_argv(args);
    if (argv < 3){
        printf("Not enough arguments specified.\nUsage: dump offset size\n");
        return 0;
    }
    unsigned int size = simple_strtoul(args[2], NULL, 16);
    unsigned int offset = simple_strtoul(args[1], NULL, 16);
    printf("Dumping %u bytes of flash\n", size);
    char* flash = (char*) malloc(sizeof(char) * size);
    mesh_flash_read(flash, offset, size);

    // print hex in 16 byte blocks
    for(unsigned int i = 0; i < size; ++i)
    {
        if (i % 16 == 0)
        {
            printf("0x%06x ", i);
        }
        printf("%02x ", flash[i]);
        if (i % 16 == 15)
        {
        printf("\n");
        }
    }
    printf("\n");

    free(flash);

    return 0;
}

int mesh_reset_flash(char **args)
{
    // 0x1000000 is all 16 MB of flash
    // the erase page size is 64 KB or 0x10000 in hex
    char* probe_cmd[] = {"sf", "erase", "0", "0x1000000"};
    cmd_tbl_t* sf_tp = find_cmd("sf");

    printf("Resetting flash. This may take more than a minute.\n");
    return sf_tp->cmd(sf_tp, 0, 4, probe_cmd);
}

/******************************************************************************/
/******************************** End MESH Commands ***************************/
/******************************************************************************/


/******************************************************************************/
/******************************** MESH Command Loop *****************************/
/******************************************************************************/

/*
    This is the main control loop for the mesh shell.
*/
void mesh_loop(void) {
    char *line;
    char **args;
    int status = 1;

    memset(user.name, 0, MAX_STR_LEN);
    memset(user.pin, 0, MAX_STR_LEN);


    mesh_flash_init();
    if (mesh_is_first_table_write())
    {
        printf("Performing first time setup...\n");
        mesh_init_table();
        printf("Done!\n");
    }


    // Perform first time initialization to ensure that the default
    // games are present
    strncpy(user.name, "demo", 5);
    strncpy(user.pin, "00000000", 9);

    for(int i = 0; i < NUM_DEFAULT_GAMES; ++i)
    {
        char* install_args[] = {"install", default_games[i], '\0'};
        int ret_code = mesh_install(install_args);
        if (ret_code != 0 && ret_code != 5 && ret_code != 6)
        {
            printf("Error detected while installing default games\n");
            while(1);
        }
    }

    memset(user.name, 0, MAX_STR_LEN);
    memset(user.pin, 0, MAX_STR_LEN);

    while(1)
    {
        if (mesh_login(&user))
            continue;

        while(*(user.name)) {
            line = mesh_input(CONFIG_SYS_PROMPT);

            // This is the run_command function from common/cli.c:29
            // if this is uncommented, then it checks first in the builtins in
            // for the hush shell then for the command. This allows you to use
            // all the builtin commands when developing.
            // if (!run_command(line, 0)){
            // }

            args = mesh_split_line(line);
            status = mesh_execute(args);
            free(args);

            free(line);

            // -2 for exit
            if (status == MESH_SHUTDOWN)
                break;
        }
        if (status == MESH_SHUTDOWN)
            break;
    }
}

/******************************************************************************/
/****************************** End MESH Command Loop ***************************/
/******************************************************************************/

/******************************************************************************/
/*********************************** MESH Ext4 ********************************/
/******************************************************************************/

/*
    This is a modified version of ext4fs_iterate_dir from ext4_common.c:1994
    It has the same usage as the original function, however, it only prints out
    regular files on the partition.

    If fname is specified, then no text is written to std out and it returns 1
    if the filename is found in dir and 0 otherwise.

    If fname is not specified, then it lists all files in dir to std out.
*/
int mesh_ls_iterate_dir(struct ext2fs_node *dir, char *fname)
{
    Game game;
    unsigned int fpos = 0;
    unsigned int game_num = 1;
    int status;
    loff_t actread;
    struct ext2fs_node *diro = (struct ext2fs_node *) dir;

    if (!diro->inode_read) {
        status = ext4fs_read_inode(diro->data, diro->ino, &diro->inode);
        if (status == 0)
            return 0;
    }
    /* Search the file.  */
    while (fpos < le32_to_cpu(diro->inode.size)) {
        struct ext2_dirent dirent;

        status = ext4fs_read_file(diro, fpos,
                       sizeof(struct ext2_dirent),
                       (char *)&dirent, &actread);
        if (status < 0)
            return 0;

        if (dirent.direntlen == 0) {
            printf("Failed to iterate over directory\n");
            return 0;
        }

        if (dirent.namelen != 0) {
            char filename[dirent.namelen + 1];
            struct ext2fs_node *fdiro;
            int type = FILETYPE_UNKNOWN;

            status = ext4fs_read_file(diro,
                          fpos +
                          sizeof(struct ext2_dirent),
                          dirent.namelen, filename,
                          &actread);
            if (status < 0)
                return 0;

            fdiro = zalloc(sizeof(struct ext2fs_node));
            if (!fdiro)
                return 0;

            fdiro->data = diro->data;
            fdiro->ino = le32_to_cpu(dirent.inode);

            filename[dirent.namelen] = '\0';

            if (dirent.filetype != FILETYPE_UNKNOWN) {
                fdiro->inode_read = 0;

                if (dirent.filetype == FILETYPE_DIRECTORY)
                    type = FILETYPE_DIRECTORY;
                else if (dirent.filetype == FILETYPE_SYMLINK)
                    type = FILETYPE_SYMLINK;
                else if (dirent.filetype == FILETYPE_REG)
                    type = FILETYPE_REG;
            } else {
                status = ext4fs_read_inode(diro->data,
                               le32_to_cpu
                               (dirent.inode),
                               &fdiro->inode);
                if (status == 0) {
                    free(fdiro);
                    return 0;
                }
                fdiro->inode_read = 1;

                if ((le16_to_cpu(fdiro->inode.mode) &
                     FILETYPE_INO_MASK) ==
                    FILETYPE_INO_DIRECTORY) {
                    type = FILETYPE_DIRECTORY;
                } else if ((le16_to_cpu(fdiro->inode.mode)
                        & FILETYPE_INO_MASK) ==
                       FILETYPE_INO_SYMLINK) {
                    type = FILETYPE_SYMLINK;
                } else if ((le16_to_cpu(fdiro->inode.mode)
                        & FILETYPE_INO_MASK) ==
                       FILETYPE_INO_REG) {
                    type = FILETYPE_REG;
                }
            }

            if (fname != NULL) {
                if (type == FILETYPE_REG && strcmp(filename, fname) == 0) {
                    return 1;
                }
            } else {
                if (fdiro->inode_read == 0) {
                    status = ext4fs_read_inode(diro->data,
                                 le32_to_cpu(
                                 dirent.inode),
                                 &fdiro->inode);
                    if (status == 0) {
                        free(fdiro);
                        return 0;
                    }
                    fdiro->inode_read = 1;
                }
                switch (type) {
                case FILETYPE_REG:
                    // only print name if the user is in valid install list
                    mesh_get_game_header(&game, filename);
                    if (mesh_check_user(&game)){
                        printf("%d      ", game_num++);
                        printf("%s\n", filename);
                    }

                    break;
                default:
                    break;
                }
            }
            free(fdiro);
        }
        fpos += le16_to_cpu(dirent.direntlen);
    }
    return 0;
}

/*
    This is derived from the ext4fs_ls function in ext4fs.c:158
    It is meant to be a standalone function by setting the correct
    device to read from and then querying files from the custom mesh
    file iterator.
*/
int mesh_ls_ext4(const char *dirname, char *filename)
{
    int ret = 0;

    struct ext2fs_node *dirnode;
    int status;

    if (dirname == NULL)
        return 0;

    status = ext4fs_find_file(dirname, &ext4fs_root->diropen, &dirnode,
                  FILETYPE_DIRECTORY);
    if (status != 1) {
        printf("** Can not find directory. **\n");
        return -1;
    }

    ret = mesh_ls_iterate_dir(dirnode, filename);

    return ret ;
}

int mesh_query_ext4(const char *dirname, char *filename){

    int ret = 0;

    if(fs_set_blk_dev("mmc", "0:2", FS_TYPE_EXT) < 0){
        return -1;
    }

    // fs/fs.c:281
    ret = mesh_ls_ext4(dirname, filename);

    ext4fs_close();

    return ret;
}

/* 
    This function gets the size of a file on a ext4 partion. It uses the
    u-boot ext4 fs functions to determine the size. 
*/
loff_t mesh_size_ext4(char *fname){
    loff_t size;    

    if(fs_set_blk_dev("mmc", "0:2", FS_TYPE_EXT) < 0){
        return -1;
    }

    // fs/fs.c:281
    ext4fs_size(fname, &size);

    ext4fs_close();

    return size;
}

loff_t mesh_read_ext4(char *fname, char*buf, loff_t size){
    loff_t actually_read;

    if(fs_set_blk_dev("mmc", "0:2", FS_TYPE_EXT) < 0){
        return -1;
    }

    ext4_read_file(fname, buf, 0, size, &actually_read);

    ext4fs_close();

    return actually_read;

}

/******************************************************************************/
/******************************* End MESH Ext4 ********************************/
/******************************************************************************/

/******************************************************************************/
/************************************* Helpers ********************************/
/******************************************************************************/

void full_name_from_short_name(char* full_name, struct games_tbl_row* row)
{
    sprintf(full_name, "%s-v%d.%d", row->game_name, row->major_version, row->minor_version);
}

/*
    This function determines if the specified game is installed for the given
    user. It return 1 if it is installed and 0 if it isnt.
*/
int mesh_game_installed(char *game_name){
    struct games_tbl_row row;
    unsigned int offset = MESH_INSTALL_GAME_OFFSET;

    // loop through install table until table end is found
    for(mesh_flash_read(&row, offset, sizeof(struct games_tbl_row));
        row.install_flag != MESH_TABLE_END;
        mesh_flash_read(&row, offset, sizeof(struct games_tbl_row)))
    {
        // the most space that we could need to store the full game name
        char* full_name = (char*) malloc(snprintf(NULL, 0, "%s-v%d.%d", row.game_name, row.major_version, row.minor_version) + 1);
        full_name_from_short_name(full_name, &row);
        // check if game is installed and if it is for the specified user.
        if (strcmp(game_name, full_name) == 0 &&
            strcmp(user.name, row.user_name) == 0 &&
            row.install_flag == MESH_TABLE_INSTALLED)
        {
            free(full_name);
            return 1;
        }
        free(full_name);
        offset += sizeof(struct games_tbl_row);
    }

    return 0;
}

/*
    This function validates the arguments for mesh play. It returns 1 if the
    arguments are valid and 0 if they are not. It will print usage help and any
    pertinent warnings.
*/
int mesh_play_validate_args(char **args){
    // ensure a game name is listed
    int argv = mesh_get_argv(args);
    if (argv < 2){
        printf("No game name specified.\n");
        printf("Usage: play [GAME NAME]\n");
        return 0;
    } else if (argv > 2){
        printf("Warning, more than one argument specified, install first game specified.\n");
    }

    // assert game length is valid
    for (int count=0; args[1][count] != 0; count++){
        if (count > MAX_GAME_LENGTH) {
            printf("Specified game exceeds maximum game name length of %d\n", MAX_GAME_LENGTH);
            return 0;
        }
    }

    // assert game exists in filesystem
    if (!mesh_game_installed(args[1])){
        printf("%s is not installed for %s.\n", args[1], user.name);
        return 0;
    }

    return 1;
}

/*
    This function determines if a game exists on the ext4 partition of the
    sd card with the given game_name. It returns 1 if it is found and 0 if it
    is not.
*/
int mesh_game_exists(char *game_name)
{
    /* List all games available to download */
    return mesh_query_ext4("/", game_name) == 1;
}

/*
    This function detemrines if the specified user can install the given game.
*/
int mesh_check_user(Game *game)
{
    for (int i=0; i<game->num_users; i++){
        if (strcmp(game->users[i], user.name) == 0){
            return 1;
        }
    }

    return 0;
}

/*
    This function determines if you are downgrading the specified game.
    Returns 0 on downgrade, 1 otherwise
*/
int mesh_check_downgrade(char *game_name, unsigned int major_version, unsigned int minor_version)
{
    struct games_tbl_row row;
    unsigned int offset = MESH_INSTALL_GAME_OFFSET;
    int return_value = 0;

    for(mesh_flash_read(&row, offset, sizeof(struct games_tbl_row));
        row.install_flag != MESH_TABLE_END;
        mesh_flash_read(&row, offset, sizeof(struct games_tbl_row)))
    {
        offset += sizeof(struct games_tbl_row);

        // Ignore anyone that isn't the current user
        if (strcmp(user.name, row.user_name) != 0)
            continue;

        // ignore it if it doesn't have the same game name
        // must make a copy, otherwise, it modified game_name, which under the covers is args[1]
        char short_game_name[MAX_GAME_LENGTH + 1] = "";
        strncpy(short_game_name, game_name, MAX_GAME_LENGTH);
        strtok(short_game_name, "-");
        if (strcmp(short_game_name, row.game_name) != 0)
            continue;

        // Fail if the major version of the new game is less than the currently
        // installed game
        if (major_version < row.major_version)
        {
            return_value = 1;
        }
        // Fail if the major version of the new game is the same and the minor
        // version is less or the same
        else if (major_version == row.major_version && minor_version < row.minor_version)
        {
            return_value = 1;
        }
        // prevent a reinstall of the same version without an uninstall
        else if (major_version == row.major_version &&
            minor_version == row.minor_version &&
            row.install_flag == MESH_TABLE_INSTALLED)
        {
            return_value = return_value == 1 ? return_value : 2;
        }
    }
    return return_value;
}

/*
    This function extract the game info from the header of a game file.
*/
void mesh_get_game_header(Game *game, char *game_name){
    loff_t game_size;
    int i = 0;
    int j = 0;

    // get the size of the game
    game_size = mesh_size_ext4(game_name);

    // read the game into a buffer
    char* game_buffer = (char*) malloc(game_size + 1);
    mesh_read_ext4(game_name, game_buffer, game_size);

    // get the version, located on the first line. will always be major.minor

    // remove the string "version"
    strtok(game_buffer, ":");
    // get everything up to the first '.'. That's the major version
    char* major_version_str = strtok(NULL, ".");
    // get after the '.'. That's the minor version
    char* minor_version_str = strtok(NULL, "\n");

    // get the name, located after "name:"
    char* name = strtok(NULL, ":");
    name = strtok(NULL, "\n");

    // get the users, located after "users:"
    char* users = strtok(NULL, ":");
    users = strtok(NULL, "\n");

    // copy major and minor version into struct
    game->major_version = simple_strtoul(major_version_str, NULL, 10);
    game->minor_version = simple_strtoul(minor_version_str, NULL, 10);

    // copy name
    for (i=0; i<MAX_GAME_LENGTH && name[i] != '\0'; i++){
        game->name[i] = name[i];
    }
    game->name[i] = '\0';

    // copy users
    int strplace = 0;
    for (i=0; i<MAX_NUM_USERS && users[strplace] != '\0'; i++){
        for (j=0; j<=MAX_USERNAME_LENGTH && users[strplace] != ' ' && users[strplace] != '\0'; j++){
            game->users[i][j] = users[strplace++];
        }

        // increment past space if you are there
        if (users[strplace] == ' '){
            strplace++;
        }

        // null terminate user
        game->users[i][j] = '\0';
    }
    game->num_users = i;

    free(game_buffer);
}
/*
    This function reads in the specified game and ensures that the user is
    in the allowed users section of the game and that you are not downgrading
    a game.

    Returns:
        int: An error code representing if the game is valid or not.
            0 - No error, valid game install
            1 - Error, game does not exist
            2 - Error, user is not allowed
            3 - Error, downgrade not allowed
            4 - Error, game is already installed
*/
int mesh_valid_install(char *game_name){
    if (!mesh_game_exists(game_name)){
        printf("Game doesnt exist\n");
        return 1;
    }

    Game game;
    mesh_get_game_header(&game, game_name);

    if (!mesh_check_user(&game)){
        return 2;
    }
    if (mesh_game_installed(game_name)){
        return 4;
    }
    if (mesh_check_downgrade(game_name, game.major_version, game.minor_version)){
        return 3;
    }

    return 0;
}

/*
    This function validates the arguments for mesh_install. If the arguments are
    valid it returns 1 and otherwise returns 0.

    It implements the mesh shell install function.
*/
int mesh_install_validate_args(char **args){
    // ensure a game name is listed
    int errno = 0;

    int argv = mesh_get_argv(args);
    if (argv < 2){
        printf("No game name specified.\n");
        printf("Usage: install [GAME NAME]\n");
        return 1;
    } else if (argv > 2){
        printf("Warning, more than one argument specified, install first game specified.\n");
    }

    // assert game length is valid
    for (int count=0; args[1][count] != 0; count++){
        if (count > MAX_GAME_LENGTH) {
            printf("Specified game exceeds maximum game name length of %d\n", MAX_GAME_LENGTH);
            return 2;
        }
    }

    char *game_name = args[1];

    // assert game exists in filesystem
    errno = mesh_valid_install(game_name);
    switch (errno) {
        case 0 :
            break;
        case 1 :
            printf("Error installing %s, the game does not exist on the SD card games partition.\n", game_name);
            return 3;
        case 2 :
            printf("Error installing %s, %s is not allowed to install this game.\n", game_name, user.name);
            return 4;
        case 3 :
            printf("Error installing %s, downgrade not allowed. Later version is already installed.\n", game_name);
            return 5;
        case 4 :
            printf("Skipping install of %s, game is already installed.\n", game_name);
            return 6;
        default :
            printf("Unknown error installing game.\n");
            return -1;
    }
    if (!mesh_game_exists(args[1])){
        printf("The specified game is not available to install.\n");
        return 3;
    }

    return 0;
}

/*
    This function executes the specified command for the given user.
    It finds the command in builtin_func and then calls the function with the
    args for the given user.
*/
int mesh_execute(char **args) {
    int i;

    if (args[0] == NULL) {
        // An empty command was entered.
        return 1;
    }

    for (i = 0; i < mesh_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    printf("Not a valid command\n");
    printf("Use help to get a list of valid commands\n");
    return 1;
}

/*
    This is a helper function to convert a character point to a hex string
    beginning with 0x. This is used for converting values to u-boot parameters
    which expects hex strings.
*/
void ptr_to_string(void* ptr, char* buf)
{
    /* Given a pointer and a buffer of length 11, returns a string of the poitner */
    sprintf(buf, "0x%x", (unsigned int) ptr);
    buf[10] = 0;
}

/*
    This function determines if the sentinel is written to flash addres
    MESH_SENTINEL_LOCATION yet. If it is then it returns 1, otherwise, it returns
    0.
*/
int mesh_is_first_table_write(void)
{
    /* Initialize the table where games will be installed */
    char* sentinel = (char*) malloc(sizeof(char) * MESH_SENTINEL_LENGTH);
    int ret = 0;

    mesh_flash_read(sentinel, MESH_SENTINEL_LOCATION, MESH_SENTINEL_LENGTH);

    if (*((unsigned int*) sentinel) != MESH_SENTINEL_VALUE)
    {
        ret = 1;
    }
    free(sentinel);
    return ret;
}

/*
    This function determines if the specified user and pin is listed in the 
    mesh_users array. If it is then the user is logged in and the function
    returns 1. Otherwise, it returns 0.
*/
int mesh_validate_user(User *user)
{
    /* Validates that the username and pin match a combination
     * provisioned with the board. This is read from the
     * mesh_users.h header file.
     * Retruns 0 on success and 1 on failure. */
    for (int i = 0; i < NUM_MESH_USERS; ++i)
    {
        if (strcmp(mesh_users[i].username, user->name) == 0 &&
            strcmp(mesh_users[i].pin, user->pin) == 0)
        {
            return 0;
        }
    }
    return 1;
}

/*
    This function determines the number of builtin functions in the mesh
    shell.
*/
int mesh_num_builtins(void) {
    return sizeof(builtin_str) / sizeof(char *);
}

/*
    This function reads a line from stdin and returns a pointer to the character
    buffer containing the null terminated line. 

    This funciton allocates the charater buffer on the heap, therefore, the caller
    must free this buffer to avoid a memory leak.
*/
char* mesh_read_line(int bufsize)
{
    int position = 0;
    char *buffer = (char*) malloc(sizeof(char) * bufsize);
    int c;

    while (1) {
        // Read a character
        c = getc();

        if (position == bufsize - 1) {
            printf("\b");
        }
        if (c == '\n' || c == '\r') {
            printf("\n");
            buffer[position] = '\0';
            return buffer;
        }
        else if (c == '\b' || c == 0x7F) // backspace
        {
            if (position)
            {
                position--;
                buffer[position] = '\0';
                printf("\b \b");
            }
        }
        else {
            buffer[position] = c;
            if (position < bufsize - 1)
            {
                position++;
            }
            printf("%c", c);
        }
    }
}

/* 
    This function determines the number of arguments specified in args and
    returns that number..
*/
int mesh_get_argv(char **args){
    int count = 0;

    for (int i=0; args[i]; i++){
        count++;
    }

    return count;
}

/*
    This function is used to split a single line of command line arguments
    into an array of individual arguments. 

    It returns an array of character buffers. Both this array and the character
    buffers are allocated on the heap and therefore, it is the responsibility of
    the caller to free this memory after the arguments are used.
*/
char **mesh_split_line(char *line) {
    int bufsize = MESH_TOK_BUFSIZE, position = 0;
    char **tokens = (char**) malloc(bufsize * sizeof(char*));
    char *token, **tokens_backup;

    token = strtok(line, MESH_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += MESH_TOK_BUFSIZE;
            tokens_backup = tokens;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                free(tokens_backup);
            }
        }

        token = strtok(NULL, MESH_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

/*
    This function prompts from user input from stdin and returns a point to
    that read line. Note, this is line is created using mesh_read_line and thus
    it is the responsibility of the caller to free the character buffer.
*/
char* mesh_input(char* prompt)
{
    printf(prompt);
    return mesh_read_line(MAX_STR_LEN);
}

/*
    This function handles logging in a user. It prompts for a username and pin.
    If a valid user pin combo is read, it writes the name and pin to the user
    struct and returns 0, otherwise, it returns an error code
*/
int mesh_login(User *user) {
    User tmp_user;

    char *tmp_name, *tmp_pin;
    int retval;

    memset(user->name, 0, MAX_STR_LEN);

    do {
        tmp_name = mesh_input("Enter your username: ");
    } while (!strlen(tmp_name));

    do {
        tmp_pin = mesh_input("Enter your PIN: ");
    } while (!strlen(tmp_pin));

    strncpy(tmp_user.name, tmp_name, MAX_STR_LEN);
    strncpy(tmp_user.pin, tmp_pin, MAX_STR_LEN);

    /* if valid user, copy into user */
    retval = mesh_validate_user(&tmp_user);
    if (!retval) {
        strncpy(user->name, tmp_user.name, MAX_STR_LEN);
        strncpy(user->pin, tmp_user.pin, MAX_STR_LEN);
    } else {
        printf("Login failed. Please try again\n");
    }

    free(tmp_name);
    free(tmp_pin);

    return retval;
}
