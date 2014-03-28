#include "tapp.h"
#include "terrno.h"
#include "string.h"
#include "protocol/tlibc_xml_reader.h"


#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

static void version()
{
	printf("TApp version %s\n", TAPP_VERSION);
}

static void usage()
{
    fprintf(stderr, "Usage: program [options] file\n\n");
    fprintf(stderr, "Use program -help for a list of options\n");
}

static void help()
{
	fprintf(stderr, "Usage: program [options] file\n");  
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -version                 Print the compiler version.\n");
    fprintf(stderr, "  -help                    Print the useage.\n");
	fprintf(stderr, "  file                     Set the config file.\n");
}

void tapp_load_config(void *config, int argc, char *argv[], tapp_xml_reader_t reader)
{
	TLIBC_XML_READER xml_reader;
    const char *config_file = NULL;
    int i;
    tlibc_xml_reader_init(&xml_reader);

	for (i = 1; i < argc; ++i)
	{
		char* arg;

		arg = strtok(argv[i], " ");
		if (arg[0] == '-' && arg[1] == '-')
		{
			++arg;
		}
		
		if (strcmp(arg, "-help") == 0)
		{
			help();
			goto ERROR_RET;
		}
		else if (strcmp(arg, "-version") == 0)
		{
			version();
			goto ERROR_RET;
		}		
		else if (strcmp(arg, "-I") == 0)
		{
		    ++i;
		    if(i >= argc)
		    {
         		fprintf(stderr, "argument to '-I' is missing.\n");
		        goto ERROR_RET;
		    }
            tlibc_xml_add_include(&xml_reader, argv[i]);
			goto ERROR_RET;
		}
		else
		{
		    config_file = arg;
		    ++i;
            break;
		}

		arg = strtok(NULL, " =");
	}

	if(i != argc)
	{
    	usage();
    	goto ERROR_RET;
	}
	if(reader)
	{
    	if (config_file == NULL)
    	{
    		fprintf(stderr, "Missing config file specification\n");
    		help();
    		goto ERROR_RET;
    	}
	}



    if(tlibc_xml_reader_push_file(&xml_reader, config_file) != E_TLIBC_NOERROR)
    {
   		fprintf(stderr, "load push config file [%s] failed.\n", config_file);
        goto ERROR_RET;
    }
    
	if(reader(&xml_reader.super, config) != E_TLIBC_NOERROR)
	{
    	const TLIBC_XML_READER_YYLTYPE *lo = tlibc_xml_current_location(&xml_reader);
    	if(lo)
    	{
        	fprintf(stderr, "load xml [%s] failed at %d,%d - %d,%d.\n", lo->file_name, 
        	    lo->first_line, lo->first_column, lo->last_line, lo->last_column);
    	}
    	else
    	{
        	fprintf(stderr, "load xml [%s] failed.", config_file);
    	}   	
		
		tlibc_xml_reader_pop_file(&xml_reader);
		goto ERROR_RET;
	}
    tlibc_xml_reader_pop_file(&xml_reader);
    
	return;
ERROR_RET:
    exit(1);
}

static bool g_sigterm = false;
static bool g_sigusr1 = false;
static bool g_sigusr2 = false;


static void on_signal(int sig)
{
    switch(sig)
    {
        case SIGTERM:
            g_sigterm = true;
            break;
        case SIGUSR1:
            g_sigusr1 = true;
            break;
        case SIGUSR2:
            g_sigusr2 = true;
            break;
    }
}

TERROR_CODE tapp_loop(tapp_func_t process, useconds_t idle_usec, size_t idle_limit,
                        tapp_func_t sigusr1, tapp_func_t sigusr2)
{
    TERROR_CODE ret = E_TS_NOERROR;
    uint32_t idle_count = 0;
    struct sigaction  sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = on_signal;
	if(sigemptyset(&sa.sa_mask) != 0)
	{
	    ret = E_TS_ERRNO;
        goto done;
	}

	if((sigaction(SIGTERM, &sa, NULL) != 0)
	|| (sigaction(SIGUSR1, &sa, NULL) != 0)
	|| (sigaction(SIGUSR2, &sa, NULL) != 0))
	{
    	ret = E_TS_ERRNO;
        goto done;
	}


    g_sigterm = false;
    for(;!g_sigterm;)
    {
        if(g_sigusr1)
        {
            g_sigusr1 = false;            
            if(sigusr1)
            {
                ret = sigusr1();
                if(ret != E_TS_NOERROR)
                {
                    goto done;
                }
            }
            idle_count = 0;
        }

        if(g_sigusr2)
        {
            g_sigusr2 = false;            
            if(sigusr2)
            {
                ret = sigusr2();
                if(ret != E_TS_NOERROR)
                {
                    goto done;
                }
            }
            idle_count = 0;
        }

        
        ret = process();
        switch(ret)
        {
        case E_TS_NOERROR:
            idle_count = 0;
            break;
        case E_TS_WOULD_BLOCK:
            {
                ++idle_count;
                if(idle_count >= idle_limit)
                {
                    if((usleep(idle_usec) != 0) && (errno != EINTR))
                    {
                        ret = E_TS_ERRNO;
                        goto done;
                    }
                    idle_count = 0;                 
                }
            }
            break;
        default:
            goto done;
        }
    }   

done:
    return ret;
}
