/*
 * code parts from httpd/modules/core/mod_macro.c
 */

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"

#include "apr.h"
#include "apr_strings.h"
#include "apr_hash.h"
#include "apr_dbd.h"
#include "apr_strings.h"
#include "apr_lib.h"

#include "mod_dbd.h"

#define EXEC_SQL "ExecuteSQL"
#define DB_DRIVER "DBC_DBDriver"
#define DB_DSN "DBC_DBDSN"

typedef struct
{
    int index;                    /* current element */
    int char_index;               /* current char in element */
    int length;                   /* cached length of the current line */
    apr_array_header_t *contents; /* array of char * */
    ap_configfile_t *next;        /* next config once this one is processed */
    ap_configfile_t **upper;      /* hack: where to update it if needed */
} array_contents_t;


static char * db_dsn = NULL;
static char * db_driver = NULL;
static const apr_dbd_driver_t * apr_driver;
static apr_dbd_t *dbd = NULL;

#define empty_string_p(p) (!(p) || *(p) == '\0')
#define trim(line) while (*(line) == ' ' || *(line) == '\t') (line)++

/*
  Get next config if any.
  this may be called several times if there are continuations.
*/
static int next_one(array_contents_t * ml)
{
    if (ml->next) {
        ap_assert(ml->upper);
        *(ml->upper) = ml->next;
        return 1;
    }
    return 0;
}

/*
  returns next char if possible
  this may involve switching to enclosing config.
*/
static apr_status_t array_getch(char *ch, void *param)
{
    array_contents_t *ml = (array_contents_t *) param;
    char **tab = (char **) ml->contents->elts;

    while (ml->char_index >= ml->length) {
        if (ml->index >= ml->contents->nelts) {
            /* maybe update */
            if (ml->next && ml->next->getch && next_one(ml)) {
                apr_status_t rc = ml->next->getch(ch, ml->next->param);
                if (*ch==LF)
                    ml->next->line_number++;
                return rc;
            }
            return APR_EOF;
        }
        ml->index++;
        ml->char_index = 0;
        ml->length = ml->index >= ml->contents->nelts ?
            0 : strlen(tab[ml->index]);
    }

    *ch = tab[ml->index][ml->char_index++];
    return APR_SUCCESS;
}

/*
  returns a buf a la fgets.
  no more than a line at a time, otherwise the parsing is too much ahead...
  NULL at EOF.
*/
static apr_status_t array_getstr(void *buf, size_t bufsize, void *param)
{
    array_contents_t *ml = (array_contents_t *) param;
    char *buffer = (char *) buf;
    char next = '\0';
    size_t i = 0;
    apr_status_t rc = APR_SUCCESS;

    /* read chars from stream, stop on newline */
    while (i < bufsize - 1 && next != LF &&
           ((rc = array_getch(&next, param)) == APR_SUCCESS)) {
        buffer[i++] = next;
    }

    if (rc == APR_EOF) {
        /* maybe update to next, possibly a recursion */
        if (next_one(ml)) {
            ap_assert(ml->next->getstr);
            /* keep next line count in sync! the caller will update
               the current line_number, we need to forward to the next */
            ml->next->line_number++;
            return ml->next->getstr(buf, bufsize, ml->next->param);
        }
        /* else that is really all we can do */
        return APR_EOF;
    }

    buffer[i] = '\0';

    return APR_SUCCESS;
}

/*
  close the array stream?
*/
static apr_status_t array_close(void *param)
{
    array_contents_t *ml = (array_contents_t *) param;
    /* move index at end of stream... */
    ml->index = ml->contents->nelts;
    ml->char_index = ml->length;
    return APR_SUCCESS;
}


/*
  create an array config stream insertion "object".
  could be exported.
*/
static ap_configfile_t *make_array_config(apr_pool_t * pool,
                                          apr_array_header_t * contents,
                                          const char *where,
                                          ap_configfile_t * cfg,
                                          ap_configfile_t ** upper)
{
    array_contents_t *ls =
        (array_contents_t *) apr_palloc(pool, sizeof(array_contents_t));
    ap_assert(ls!=NULL);

    ls->index = 0;
    ls->char_index = 0;
    ls->contents = contents;
    ls->length = ls->contents->nelts < 1 ?
        0 : strlen(((char **) ls->contents->elts)[0]);
    ls->next = cfg;
    ls->upper = upper;

    return ap_pcfg_open_custom(pool, where, (void *) ls, array_getch, array_getstr, array_close);
}

/**
 * Execute SQL Command and do replacements when ExecuteSQL appears. When UseTemplate appears in the result string, do replacements in the given template.
 * @param cmd The Apache command environment
 * @param dummy not used
 * @param arg The SQL string
 * @return char* NULL when no error occurs, an error string when an error occurs
 */
static const char *exec_sql(cmd_parms * cmd, void *dummy, const char *arg)
{
    char *sql, *line = NULL;
	apr_dbd_results_t *res = NULL;
	apr_dbd_row_t *row = NULL;
	apr_status_t astat;
	
	if((sql = ap_getword_conf(cmd->temp_pool, &arg)) == NULL) return "No command given.";
	if(db_dsn == NULL || !strcmp(db_dsn, "")) return "Please define db_dsn first!";

	//if((astat = apr_dbd_open(apr_driver, cmd->temp_pool, db_dsn, &dbd)) != APR_SUCCESS) return apr_dbd_error(apr_driver, dbd, astat);
	const char * error;
	if((astat = apr_dbd_open_ex(apr_driver, cmd->temp_pool, db_dsn, &dbd, &error)) != APR_SUCCESS)
	{
		fprintf(stderr, "Error: %s\n", error);
		return apr_dbd_error(apr_driver, dbd, astat);
	}
	
	char *where = apr_psprintf(cmd->temp_pool, "File '%s' (%d)", cmd->config_file->name, cmd->config_file->line_number);
	
	apr_array_header_t *contents = apr_array_make(cmd->temp_pool, 1, sizeof(char *));
	
	int sqlstate = apr_dbd_select(apr_driver, cmd->temp_pool, dbd, &res, sql, 1);
	if(sqlstate != 0) return apr_dbd_error(apr_driver, dbd, sqlstate);
	
	int rows = apr_dbd_num_tuples(apr_driver, res);
	int cols = apr_dbd_num_cols(apr_driver, res);
	while(!apr_dbd_get_row(apr_driver, cmd->temp_pool, res, &row, -1))
	{
		char **new = apr_array_push(contents);
		line = apr_pstrdup(cmd->temp_pool, "");
		for(int i = 0; i < cols; i++)
		{
			char * col = (char*)apr_dbd_get_entry(apr_driver, row, i);
			line = apr_pstrcat(cmd->temp_pool, line, " ", col, NULL);
			//debug_printf("Col %d: %s, line: %s\n", i, col, line);
		}
		
		line = apr_pstrcat(cmd->temp_pool, line, "\n", NULL);
		*new = apr_pstrdup(cmd->temp_pool, line);
	}
	apr_dbd_close(apr_driver, dbd);

	/* the current "config file" is replaced by a string array...
       at the end of processing the array, the initial config file
       will be returned there (see next_one) so as to go on. */
    cmd->config_file = make_array_config(cmd->temp_pool, contents, where, cmd->config_file, &cmd->config_file);
	return NULL;
}

/**
 * Set the db_driver global variable when DBC_DBDriver directive appears in config file.
 * @param cmd The Apache command environment
 * @param mconfig not used
 * @param arg The value in the directive
 * @return char* NULL when no error occurs, an error string when an error occurs
 */
static const char *set_db_driver(cmd_parms *cmd, void *mconfig, const char *arg)
{
	db_driver = apr_pstrdup(cmd->temp_pool, arg);
	apr_status_t astat;
	apr_dbd_init(cmd->temp_pool);
	if((astat = apr_dbd_get_driver(cmd->temp_pool, db_driver, &apr_driver))	!= APR_SUCCESS) return "Failed to get driver structure!";
	return NULL;
}

/**
 * Set the db_dsn global variable when DBC_DBDSN directive appears in config file.
 * @param cmd The Apache command environment
 * @param mconfig not used
 * @param arg The value in the directive
 * @return char* NULL when no error occurs, an error string when an error occurs
 */
static const char *set_dsn(cmd_parms *cmd, void *mconfig, const char *arg)
{
	db_dsn = apr_pstrdup(cmd->temp_pool, arg);
	return NULL;
}

static const command_rec mod_cmds[] = {
    AP_INIT_RAW_ARGS(EXEC_SQL, exec_sql, NULL, EXEC_ON_READ | OR_ALL, "Use of a command."),
    AP_INIT_TAKE1(DB_DRIVER, set_db_driver, NULL, EXEC_ON_READ | OR_ALL, "Set DB Driver."),
    AP_INIT_TAKE1(DB_DSN, set_dsn, NULL, EXEC_ON_READ | OR_ALL, "Set DB DSN."),
	{NULL}
};

AP_DECLARE_MODULE(db_config) = {
    STANDARD20_MODULE_STUFF,    /* common stuff */
        NULL,                   /* create per-directory config */
        NULL,                   /* merge per-directory config structures */
        NULL,                   /* create per-server config structure */
        NULL,                   /* merge per-server config structures */
        mod_cmds,               /* configuration commands */
        NULL                    /* register hooks */
};
