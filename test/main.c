
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include "../src/starter.h"

#define ENGINE_VERSION_STR      "Navaro Engine Demo v '" __DATE__ "'"

#define OPTION_ID_HELP              4
#define OPTION_ID_VERBOSE           6
#define OPTION_ID_LIST              7
#define OPTION_ID_CONFIG_FILE       8
#define OPTION_COMMENT_MAX          256

struct option opt_parm[] = {
    { "help",no_argument,0,OPTION_ID_HELP },
    { "verbose",no_argument,0,OPTION_ID_VERBOSE },
    { "list",no_argument,0,OPTION_ID_LIST },
    { "config",required_argument,0,OPTION_ID_CONFIG_FILE },
    { 0,0,0,0 },
};

char *              opt_file = 0;
bool                opt_verbose = false ;
bool                opt_list = false ;
char *              opt_config_file = 0;


void
usage(char* comm)
{
    printf (
        "usage:\n"
        "  %s to compile and start an Engine Machine definition file.\n\n"
        "  %s <file> [OPTIONS]\n"
        "    <file>                Engine Machine definition file.\n"
        "    --help                Shows this message.\n"
        "    --verbose             Verbose output.\n"
        "    --list                Lista all Actions, Events and Constants.\n"
        "    --config              Configuration file or \"registry\" (default file.cfg).\n"
        ,
        ENGINE_VERSION_STR,
        comm);
    exit (0);
}

static int32_t  out(void* ctx, uint32_t out, const char* str) ;
static void     list(void* ctx, starter_list_t type, const char * name, const char* description) ;
static char *   get_config_file(void) ;


int
main(int argc, char* argv[])
{
    char c;
    int opt_index = 0;
    int32_t res ;
    printf (ENGINE_VERSION_STR) ;
    printf ("\r\n\r\n") ;

    /*
     * Parse the command line parameters.
     */
    while ((c = getopt_long (argc, (char *const *) argv, "-h", opt_parm, &opt_index)) != -1) {
        switch (c) {
        case 1:
            opt_file = optarg;
            break;

        case 'h':
        case OPTION_ID_HELP:
            usage (argv[0]);
            return 0;

        case OPTION_ID_VERBOSE:
            opt_verbose = true ;
            break;

        case OPTION_ID_LIST:
            opt_list = true ;
            break;

        case OPTION_ID_CONFIG_FILE:
            opt_config_file = optarg ;
            break ;

         }

    }

    if (opt_list) {
        /*
         * Dump all parts loaded
         */
        starter_parts_list (0, list) ;
        if (!opt_file) return 0 ;

    }

    if (!opt_file) {
        /*
         * No Machine Definition File. Exit.
         */
        usage (argv[0]);
        return 0;

    }

     /*
      * Read the Machine Definition File specified on the command line.
      */
     FILE * fp;
     fp = fopen(opt_file, "rb");
     if (fp == NULL) {
         printf("terminal failure: unable to open file \"%s\" for read.\r\n", opt_file);
         return 0;

     }
     fseek(fp, 0L, SEEK_END);
     long sz = ftell(fp);
     fseek(fp, 0L, SEEK_SET);
     char * buffer = malloc (sz) ;
     if (!buffer) {
         printf("terminal failure: out of memory.\r\n");
         return 0;

     }
     long num = fread( buffer, 1, sz, fp );
     if (!num) {
         printf("terminal failure: unable to read file \"%s\".\r\n", opt_file);
         return 0;

     }
     fclose(fp);

     /*
      * Compile the Machine Definition File and start the Engine.
      */
     printf("starting \"%s\"...\r\n\r\n", opt_file);
     starter_init (get_config_file ()) ;
     res = starter_start_ex (buffer, sz, 0, out, opt_verbose) ;
     free (buffer) ;

     if (res) {
        /*
         * Starting Engine failed.
         */
        printf("starting \"%s\" failed with %d\r\n\r\n",
                opt_file, (int) res);
        starter_stop () ;
        return 0 ;

     }

     /*
      * Engine is running now. Read the console input and generate events
      * for the characters read. The characters are fired into the Engine as
      * console events.
      */
     do {
         c = getchar() ;
         ENGINE_EVENT_CONSOLE_CHAR(c) ;
     } while (c != 'q') ;


     starter_stop () ;

     return 0;
}

static char *
get_config_file (void)
{
    static char config_file[FILENAME_MAX+4] ;
    memset (config_file, 0, FILENAME_MAX) ;

    if (opt_config_file) {
        strncpy(config_file, opt_config_file, FILENAME_MAX-1);

    }
    else if (opt_file) {

        strncpy(config_file,opt_file,FILENAME_MAX-1);

        char *end = config_file + strlen(config_file);

        while (end > config_file && *end != '.') {
            --end;
        }

        if (end > config_file) {
            *end = '\0';
        }

        strcat (config_file, ".cfg") ;


    }

    return (char*) config_file ;
}

static void
list(void* ctx, starter_list_t type, const char * name, const char* description)
{
    static starter_list_t t = typeNone ;
    if (t != type) {
        t = type ;
        const char * type_names[] = {"", "Actions", "Events", "Constatnts" } ;
        printf("%s:\r\n", type_names[t]) ;

    }

    printf ("    %-24s %s\r\n", name, description) ;
}

static int32_t
out(void* ctx, uint32_t out, const char* str)
{
    printf ("%s", str) ;
    size_t len = strlen(str) ;
    if (str[len-1] != '\n') printf ("\r\n") ;

    return 0 ;
}

