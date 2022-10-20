/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN 0
#define STDOUT 1
#define STDERR 2

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(void *arg)
{
    int client = (intptr_t)arg;
    char buf[1024];
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0; /* becomes true if server decides this is a CGI
                  * program */
    char *query_string = NULL;

    numchars = get_line(client, buf, sizeof(buf));
    i = 0;
    j = 0;
    // copy buf to method
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j = i;
    // method end with null character
    method[i] = '\0';

    // only accpet GET and POST request
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    while (ISspace(buf[j]) && (j < numchars))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    sprintf(path, "htdocs%s", url);
    // if url end with '/'
    if (path[strlen(path) - 1] == '/')
        // default is index
        strcat(path, "index.html");

    // copy file from path to st, if fail
    if (stat(path, &st) == -1)
    {
        while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        if ((st.st_mode & S_IXUSR) ||
            (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH))
            cgi = 1;
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    // output error message
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    // read from file and write to buffer
    fgets(buf, sizeof(buf), resource);

    // Loop until end of file
    while (!feof(resource))
    {
        // send buffer to client
        send(client, buf, strlen(buf), 0);

        // read from file and write to buffer
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    // output error message
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    // print error message
    // sc is the string that is printed
    perror(sc);

    // exit program with error
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    // initialize buffer
    buf[0] = 'A';
    buf[1] = '\0';

    // if method is GET, read and discard headers
    if (strcasecmp(method, "GET") == 0)
        // while buffer is not empty and not end of header
        while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
            // read from client and write to buffer
            numchars = get_line(client, buf, sizeof(buf));
    // if method is POST, read content length
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        // read from client and write to buffer
        numchars = get_line(client, buf, sizeof(buf));

        // while buffer is not empty and not end of header
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            // if contxt of buffer is "Content-Length:"
            if (strcasecmp(buf, "Content-Length:") == 0)
                // get content length
                content_length = atoi(&(buf[16]));
            // read from client and write to buffer
            numchars = get_line(client, buf, sizeof(buf));
        }
        // if the begin of buffer is not "Content-Length:"
        if (content_length == -1)
        {
            // send bad request
            bad_request(client);
            return;
        }
    }
    else /*HEAD or other*/
    {
    }

    // if output pipe fails
    if (pipe(cgi_output) < 0)
    {
        // send error message
        cannot_execute(client);
        return;
    }
    // if input pipe fails
    if (pipe(cgi_input) < 0)
    {
        cannot_execute(client);
        return;
    }

    // fork process
    if ((pid = fork()) < 0)
    {
        cannot_execute(client);
        return;
    }

    // send HTTP header
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    // if child process
    if (pid == 0) /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        // duplicate pipe
        dup2(cgi_output[1], STDOUT);
        dup2(cgi_input[0], STDIN);

        // close pipe
        close(cgi_output[0]);
        close(cgi_input[1]);

        // output method name
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);

        // if method is GET
        if (strcasecmp(method, "GET") == 0)
        {
            // output query string
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        // if method is POST
        else
        { /* POST */
            // output content length
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        // execute CGI script
        execl(path, NULL);
        exit(0);

        // if parent process
    }
    else
    { /* parent */
        // close pipe
        close(cgi_output[1]);
        close(cgi_input[0]);

        // if method is POST
        if (strcasecmp(method, "POST") == 0)
            // send content to CGI script
            for (i = 0; i < content_length; i++)
            {
                // read from client and write to input pipe
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        // read from output pipe and write to client
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        // close pipe
        close(cgi_output[0]);
        close(cgi_input[1]);
        // wait for child process to finish
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        // n is number of size receive by client(in byte)
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return (i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename; /* could use filename to determine file type */

    // print out the HTTP header
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    // print the error message
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
    {
        error_die("setsockopt failed");
    }
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0) /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return (httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    // print the error message
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/****************************/
/**********thread pool*******/
/****************************/

typedef struct tpool_work{
   void* (*work_routine)(void*); //function to be called
   void* args;                   //arguments 
   struct tool_work* next;
}tpool_work_t;
 
typedef struct tpool{
   size_t               shutdown;       //is tpool shutdown or not, 1 ---> yes; 0 ---> no
   size_t               maxnum_thread;  // maximum of threads
   pthread_t            *thread_id;     // a array of threads
   tpool_work_t*        tpool_head;     // tpool_work queue
   pthread_cond_t       queue_ready;    // condition varaible
   pthread_mutex_t      queue_lock;     // queue lock
}tpool_t;

static void *work_routine(void *args)
{
    tpool_t *pool = (tpool_t *)args;
    tpool_work_t *work = NULL;

    while (1)
    {
        pthread_mutex_lock(&pool->queue_lock);
        // if there is no works and pool is not shutdown, it should be suspended for being awake
        while (!pool->tpool_head && !pool->shutdown)
        { 
            pthread_cond_wait(&pool->queue_ready, &pool->queue_lock);
        }

        if (pool->shutdown)
        {
            // pool shutdown,release the mutex and exit
            pthread_mutex_unlock(&pool->queue_lock); 
            pthread_exit(NULL);
        }

        /* tweak a work*/
        work = pool->tpool_head;
        pool->tpool_head = (tpool_work_t *)pool->tpool_head->next;
        pthread_mutex_unlock(&pool->queue_lock);

        work->work_routine(work->args);

        free(work);
    }
    return NULL;
}

int create_tpool(tpool_t **pool, size_t max_thread_num)
{
    (*pool) = (tpool_t *)malloc(sizeof(tpool_t));
    if (NULL == *pool)
    {
        printf("malloc tpool_t failed!\n");
        exit(-1);
    }
    (*pool)->shutdown = 0;
    (*pool)->maxnum_thread = max_thread_num;
    (*pool)->thread_id = (pthread_t *)malloc(sizeof(pthread_t) * max_thread_num);
    if ((*pool)->thread_id == NULL)
    {
        printf("init thread id failed");
        exit(-1);
    }
    (*pool)->tpool_head = NULL;
    if (pthread_mutex_init(&((*pool)->queue_lock), NULL) != 0)
    {
        printf("initial mutex failed");
        exit(-1);
    }

    if (pthread_cond_init(&((*pool)->queue_ready), NULL) != 0)
    {
        printf("initial condition variable failed");
        exit(-1);
    }

    for (size_t i = 0; i < max_thread_num; i++)
    {
        if (pthread_create(&((*pool)->thread_id[i]), NULL, work_routine, (void *)(*pool)) != 0)
        {
            printf("pthread_create failed!\n");
            exit(-1);
        }
    }
    return 0;
}

void destroy_tpool(tpool_t *pool)
{
    tpool_work_t *tmp_work;

    if (pool->shutdown)
    {
        return;
    }
    pool->shutdown = 1;

    pthread_mutex_lock(&pool->queue_lock);
    pthread_cond_broadcast(&pool->queue_ready);
    pthread_mutex_unlock(&pool->queue_lock);

    for (size_t i = 0; i < pool->maxnum_thread; i++)
    {
        pthread_join(pool->thread_id[i], NULL);
    }
    free(pool->thread_id);
    while (pool->tpool_head)
    {
        tmp_work = pool->tpool_head;
        pool->tpool_head = (tpool_work_t *)pool->tpool_head->next;
        free(tmp_work);
    }

    pthread_mutex_destroy(&pool->queue_lock);
    pthread_cond_destroy(&pool->queue_ready);
    free(pool);
}

int add_task_to_tpool(tpool_t *pool, void *(*routine)(void *), void *args)
{
    tpool_work_t *work, *member;

    if (!routine)
    {
        printf("rontine is null!\n");
        return -1;
    }

    work = (tpool_work_t *)malloc(sizeof(tpool_work_t));
    if (!work)
    {
        printf("malloc work error!");
        return -1;
    }

    work->work_routine = routine;
    work->args = args;
    work->next = NULL;

    pthread_mutex_lock(&pool->queue_lock);
    member = pool->tpool_head;
    if (!member)
    {
        pool->tpool_head = work;
    }
    else
    {
        while (member->next)
        {
            member = (tpool_work_t *)member->next;
        }
        member->next = work;
    }

    // notify the pool that new task arrived!
    pthread_cond_signal(&pool->queue_ready);
    pthread_mutex_unlock(&pool->queue_lock);
    return 0;
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    // pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    // create thread pool
    tpool_t* pool = NULL;
    if(0 != create_tpool(&pool, 4)){
        printf("create_tpool failed!\n");
        return -1;
    }
    while (1)
    {
        // connect to client
        client_sock = accept(server_sock,
                             (struct sockaddr *)&client_name,
                             &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        /* accept_request(&client_sock); */
        add_task_to_tpool(pool, accept_request, (void*)client_sock );
        //if (pthread_create(&newthread, NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
        //perror("pthread_create");
    }

    close(server_sock);
    destroy_tpool(pool);

    return (0);
}
