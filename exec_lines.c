//Oleksandra Ruzhytska 3.1
//Maria Garcia Miñarro 3.1
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_WARN  1
#define LOG_LEVEL_INFO  2

// Archivo donde se guardarán los logs
const char *archivoLog = "./logs.log";

// Macro de logging
#define LOG(level, msg, ...) \
    do { \
        FILE *file = fopen(archivoLog, "a"); \
        if (file) { \
            const char *level_str[] = {"ERROR", "WARN", "INFO"}; \
            fprintf(file, "[%s] %s:%d: " msg "\n", level_str[level], __FILE__, __LINE__, ##__VA_ARGS__); \
            fclose(file); \
        } \
    } while(0)

#define DEFAULT_BUFFER_SIZE 16
#define MIN_BUF_SIZE 1
#define MAX_BUF_SIZE 8192
#define DEFAULT_LINE_SIZE 32
#define MIN_NUM_PROCS 1
#define MAX_NUM_PROCS 8
#define MIN_LINE_SIZE 16
#define MAX_LINE_SIZE 1024


volatile sig_atomic_t procesosActivos = 0;
volatile sig_atomic_t hijos_terminados = 0;
volatile sig_atomic_t error_hijos = 0; //Para controlar si algún hijo ha terminado con error


void print_help(char* program_name)
{
    fprintf(stderr, "Uso: %s [-b BUF_SIZE] [-l MAX_LINE_SIZE] [-p NUM_PROCS]\n Lee de la entrada estándar una secuencia de líneas conteniendo órdenes\n para ser ejecutadas y lanza los procesos necesarios para ejecutar cada\n línea, esperando a su terminación para ejecutar la siguiente.\n \t-b BUF_SIZE Tamaño del buffer de entrada 1<=BUF_SIZE<=8192\n \t-l MAX_LINE_SIZE Tamaño máximo de línea 16<=MAX_LINE_SIZE<=1024\n \t-p NUM_PROCS Número de procesos en ejecución de forma simultánea (1 <= NUM_PROCS <= 8)\n", program_name);
}

void separar_en_tokens(char *line, char **argv) //divide la línea en tokens separados por espacios
{
    int i = 0;
    char *token = strtok(line, " ");
    while (token != NULL && i < 127)
    {
        argv[i++] = token;
        token = strtok(NULL, " ");
    }
    argv[i] = NULL;
}

char *trim(char *s) 
{
    if (!s) return s;
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && (*end == ' ' || *end == '\t' || *end == '\n'))
        *end-- = '\0';
    return s;
}

/* Solamente dejamos que aparezca uno de ellos, por lo que si hay más de uno, deberéis sacar un
mensaje de error (ver los ejemplos de ejecución*/
int comprobarRedireccion(const char * line) 
{
    int count = 0;
    const char* p = line;
    while(*p != '\0')
    {
        if(*p == '>')
        {
            if(*(p+1) == '>')
            {
                LOG(LOG_LEVEL_INFO,"Detectado >>");
                count++;
                p+=2;
            } else {
                LOG(LOG_LEVEL_INFO,"Detectado >");
                count++;
                p++;
            }
        } else if (*p == '<' || *p == '|')
        {
            if (*p == '<') LOG(LOG_LEVEL_INFO,"Detectado <");
            else LOG(LOG_LEVEL_INFO,"Detectado |");
            count ++;
            p++;
        } else 
            p++;
    
        if(count > 1) 
        {
            fprintf(stderr, "Error, más de un operador de redirección.\n");
            return -1;
        } 
    }
    return (count == 0) ? 0 : 1;
}


void ejecutarComandoSimple(char *line, ssize_t numLine)
{
    LOG(LOG_LEVEL_INFO, "Ejecutando comando simple en línea %zu: \"%s\"", numLine, line);
    switch (fork())
    {
        case -1:
            perror("fork()");
            exit(EXIT_FAILURE);
        case 0:
        {
            char *argv[128];
            separar_en_tokens(trim(line), argv);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull != -1) 
            {
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            execvp(argv[0], argv);
            perror("execvp()");
            exit(EXIT_FAILURE);
        }
        default:
        {
            int status;
            if (wait(&status) == -1) 
            {
                perror("wait()");
                exit(EXIT_FAILURE);
            }
            /*cuando se manda una señal externa para matar el proceso */
            if (WIFSIGNALED(status)) 
            {
                fprintf(stderr, "Error al ejecutar la línea %zu. Terminación anormal por señal %d.\n",numLine, WTERMSIG(status));
                return; 
            }
/* cuando el programa acaba por un error o algo interno*/
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) 
            {
                fprintf(stderr, "Error al ejecutar la línea %zu. Terminación normal con código %d.\n",numLine, WEXITSTATUS(status));
                //kill(0, SIGTERM); Error dejar que los hijos terminen solos
                //while(wait(NULL) > 0);	//esperar todos los hijos
               //No se mata el proceso, se propaga el error al proceso padre
                exit(EXIT_FAILURE);
            }
            
            LOG(LOG_LEVEL_INFO, "Comando simple en línea %zu finalizado correctamente", numLine);
            return;
        }
    }
}

void ejecutarComandoTuberia(char *line, ssize_t numLine)
{
    LOG(LOG_LEVEL_INFO, "Ejecutando tubería en línea %zu: \"%s\"", numLine, line);
    char *pipe_pos = strchr(line, '|');
    if(!pipe_pos)
        return;
    int pipefds[2]; /* Descriptores de fichero de la tubería */
   
    //Separar comandos 
    *pipe_pos = '\0';
    char * orden_left = trim(line);
    char * orden_right = trim(pipe_pos + 1);

    if (pipe(pipefds) == -1) /* Paso 0: Creación de la tubería */
    {
        perror("pipe()");
        exit(EXIT_FAILURE);
    }

    //Creación del hijo izquierdo de la tubería
    /* Paso 1: Creación del hijo izquierdo de la tubería */
    switch (fork())
    {
        case -1:
            perror("fork(1)");
            exit(EXIT_FAILURE);
            break;
        case 0: /* Hijo izquierdo de la tubería */
        {
            /* Paso 2: El extremo de lectura no se usa */
            if (close(pipefds[0]) == -1)
            {
                perror("close(1)");
                exit(EXIT_FAILURE);
            }
            /* Paso 3: Redirige la salida estándar al extremo de escritura de la tubería */
            if (dup2(pipefds[1], STDOUT_FILENO) == -1)
            {
                perror("dup2(1)");
                exit(EXIT_FAILURE);
            }
            /* Paso 4: Cierra el descriptor duplicado */
            if (close(pipefds[1]) == -1)
            {
                perror("close(2)");
                exit(EXIT_FAILURE);
            }
            /* Paso 5: Ejecutar */
            char * argv[128];
            separar_en_tokens(orden_left, argv);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull != -1) 
            {
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            execvp(argv[0], argv);
            perror("execvp(izquierdo)");
            exit(EXIT_FAILURE);
        }
        default: /* El proceso padre continúa... */
            break;
    }
        
        
    //Creación del hijo derecho de la tubería
    switch (fork())
    {
        case -1:
            perror("fork(2)");
            exit(EXIT_FAILURE);
            break;
        case 0: /* Hijo derecho de la tubería  */
        {
            /* Paso 7: El extremo de escritura no se usa */
            if (close(pipefds[1]) == -1)
            {
                perror("close(3)");
                exit(EXIT_FAILURE);
            }
            /* Paso 8: Redirige la entrada estándar al extremo de lectura de la tubería */
            if (dup2(pipefds[0], STDIN_FILENO) == -1)
            {
                perror("dup2(2)");
                exit(EXIT_FAILURE);
            }
            /* Paso 9: Cierra el descriptor duplicado */
            if (close(pipefds[0]) == -1)
            {
                perror("close(4)");
                exit(EXIT_FAILURE);
            }
            /* Paso 10: Ejecutar */
            char * argv[128];
            separar_en_tokens(orden_right, argv);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull != -1) 
            {
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            execvp(argv[0], argv);
            perror("execvp(derecho)");
            exit(EXIT_FAILURE);
        }
        default: /* El proceso padre continúa... */
            break;
    }

    /* El proceso padre cierra los descriptores de fichero no usados */
    if (close(pipefds[0]) == -1)
    {
        perror("close(pipefds[0])");
        exit(EXIT_FAILURE);
    }
    if (close(pipefds[1]) == -1)
    {
        perror("close(pipefds[1])");
        exit(EXIT_FAILURE);
    }
    //Verificar la terminación de ambos hijos
    int status1, status2;
    if (wait(&status1) == -1)
    {
        perror("wait(1)");
        exit(EXIT_FAILURE);
    }
    if (wait(&status2) == -1)
    {
        perror("wait(2)");
        exit(EXIT_FAILURE);
    }

    //Verificar errores
    if ((WIFEXITED(status1) && WEXITSTATUS(status1) != 0) || (WIFEXITED(status2) && WEXITSTATUS(status2) != 0))
    {
        fprintf(stderr, "Error al ejecutar la línea %zu. Terminación con error de uno de los comandos de la tubería.\n", numLine);
        exit(EXIT_FAILURE);
    }

    LOG(LOG_LEVEL_INFO, "Tubería en línea %zu finalizada correctamente", numLine);
}
       
void manejador_sigchld(int signal)
{
    int saved_errno = errno;
    int status;
    pid_t pid;

    if (signal == SIGCHLD)
    {
        while((pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            procesosActivos--;
            hijos_terminados++;
            LOG(LOG_LEVEL_INFO, "Child %d terminó (procesos activos ahora: %d, hijos_terminados: %d)", pid, (int)procesosActivos, (int)hijos_terminados);
            
            //Detectar si algún hijo terminó con error 
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) 
            {
                error_hijos = 1; //Marcar que hubo un error en algún hijo
            }
        }
    }

    errno = saved_errno;
}

void instala_manejador_signal(int signal, void (*signal_handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejador_sigchld;
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signal, &sa, NULL) == -1) {
        perror("sigaction()");
        exit(EXIT_FAILURE);
    }
    LOG(LOG_LEVEL_INFO, "Instalado manejador SIGCHLD");
}


void ejecutarComandoRedireccion(char *line, ssize_t numLine)
{
    LOG(LOG_LEVEL_INFO, "Ejecutando redirección en línea %zu: \"%s\"", numLine, line);
    char *redir_pos = '\0';
    int fd;
    char *orden = line;
    char *file = NULL;
    int tipo = -1; //tipo de redirección: 0 - entrada <; 1 - salida >; 2 - añadir salida >>
   
    if ((redir_pos = strstr(line, ">>"))) 
    {
        tipo = 2;
        *redir_pos = '\0';
        file = trim(redir_pos + 2); 
    }
    else if((redir_pos = strstr(line, ">")))
    {
        tipo = 1;
        *redir_pos = '\0';
        file = trim(redir_pos + 1);
    }
    else if((redir_pos = strstr(line, "<")))
    {
        tipo = 0;
        *redir_pos = '\0';
        file = trim(redir_pos + 1);
    }
   
    if (file == NULL || *file == '\0') 
    {
        fprintf(stderr, "Error: nombre de archivo inválido en la línea %zu.\n",numLine);
        exit(EXIT_FAILURE);
    }
    
    switch (fork())
    {
        case -1: /* fork() falló */
            perror("fork()");
            exit(EXIT_FAILURE);
            break;
        case 0:           
            if (tipo == 0)
            {
                fd = open(file, O_RDONLY);
                if (fd == -1) { perror("open()"); exit(EXIT_FAILURE); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            } else if (tipo == 1)
            {
                fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) { perror("open()"); exit(EXIT_FAILURE); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            } else if (tipo == 2)
            {
                fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd == -1) { perror("open()"); exit(EXIT_FAILURE); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            if (fd == -1)
            {
                perror("open()");
                exit(EXIT_FAILURE);
            }
            char *argv[128];
            separar_en_tokens(trim(orden), argv);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull != -1) 
            {
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            execvp(argv[0], argv);
            perror("execvp()");
            exit(EXIT_FAILURE);
        
        default:                  /* Ejecución del proceso padre tras fork() con éxito */
        {
            int status;
            if (wait(&status) == -1) 
            {
                perror("wait()");
                exit(EXIT_FAILURE);
            }
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) 
            {
                //ERROR Matar a los hijos en vez de esperarlos
                //while(wait(NULL) > 0);	//esperar todos los hijos
                //if(errno != ECHILD)
                 //   perror("wait()");
                exit(EXIT_FAILURE);
            } else if (WIFSIGNALED(status)) 
            {
                fprintf(stderr, "Error al ejecutar la línea %zu. Terminación anormal por señal %d.\n", numLine, WTERMSIG(status));
                exit(EXIT_FAILURE);
            }
            LOG(LOG_LEVEL_INFO, "Redirección en línea %zu finalizada correctamente", numLine);
            break;
        }
    }       
       
}

void ejecutarLinea(char *line, ssize_t numLine) 
{
    line = trim(line);
    if(strchr(line, '|')) {
        LOG(LOG_LEVEL_INFO, "Línea %zu contiene tubería", numLine);
        ejecutarComandoTuberia(line, numLine);
    } else if (strchr(line, '>') || strchr(line, '<')) {
        LOG(LOG_LEVEL_INFO, "Línea %zu contiene redirección", numLine);
        ejecutarComandoRedireccion(line, numLine);
    } else {
        ejecutarComandoSimple(line, numLine);
    }
}
 
void esperarTodosLosHijos()
{
    LOG(LOG_LEVEL_INFO, "Esperando a todos los hijos restantes (procesosActivos=%d)", (int)procesosActivos);
    int status;
    while(waitpid(-1, &status, 0) > 0)
    {
        // Verificar errores
        if (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0))
        {
            error_hijos = 1;
        }
    }
    if(errno != ECHILD)
    {
        perror("waitpid()");
        exit(EXIT_FAILURE);
    }
}
 
int main(int argc, char *argv[])
{
    instala_manejador_signal(SIGCHLD, manejador_sigchld);
    
    // CORREGIDO:  Configurar máscaras de señales para evitar race conditions
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    
    int opt;
    int bufSize = DEFAULT_BUFFER_SIZE;
    int lineSize = DEFAULT_LINE_SIZE;
    int numProcs = MIN_NUM_PROCS;

    LOG(LOG_LEVEL_INFO, "Programa iniciado");

    optind = 1;
    while ((opt = getopt(argc, argv, "b:l:p:h")) != -1)
    {
        switch (opt)
        {
        case 'b':
            bufSize = atoi(optarg);
            LOG(LOG_LEVEL_INFO, "Opción -b:  bufSize=%d", bufSize);
            break;
        case 'l':
            lineSize = atoi(optarg);
            LOG(LOG_LEVEL_INFO, "Opción -l: lineSize=%d", lineSize);
            break;
        case 'p':
            numProcs = atoi(optarg);
            LOG(LOG_LEVEL_INFO, "Opción -p:  numProcs=%d", numProcs);
            break;
        case 'h':
            print_help(argv[0]);
            exit(EXIT_SUCCESS);
        default:
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    
    if (bufSize < MIN_BUF_SIZE || bufSize > MAX_BUF_SIZE)
    {
        fprintf(stderr, "Error: El tamaño de buffer tiene que estar entre 1 y 8192.\n");
        exit(EXIT_FAILURE);
    }
    
    if (numProcs < MIN_NUM_PROCS || numProcs > MAX_NUM_PROCS)
    {
        fprintf(stderr, "Error: El número de procesos de ejecución tiene que estar entre 1 y 8.\n");
        exit(EXIT_FAILURE);
    }
    
    if (lineSize < MIN_LINE_SIZE || lineSize > MAX_LINE_SIZE)
    {
        fprintf(stderr, "Error: El tamaño de línea tiene que estar entre 16 y 1024.\n");
        exit(EXIT_FAILURE);
    }
    
    char* buf = (char *) malloc(bufSize);
    if(buf == NULL)
    {
        perror("malloc(buf)");
        exit(EXIT_FAILURE);
    }
    
    char* line = (char *) malloc(lineSize);
    if(line == NULL)
    {
        perror("malloc(line)");
        free(buf);
        exit(EXIT_FAILURE);
    }
    
    LOG(LOG_LEVEL_INFO, "Buffers asignados:  bufSize=%d, lineSize=%d", bufSize, lineSize);
    
    ssize_t num_read;
    ssize_t line_pos = 0;
    ssize_t numLine = 1;
    
    LOG(LOG_LEVEL_INFO, "Comenzando a leer desde stdin");
    while ((num_read = read(STDIN_FILENO, buf, bufSize)) > 0)
    {
        for (ssize_t i = 0; i < num_read; i++)
        {
            char c = buf[i];
            if (line_pos >= lineSize)
            {
                line[line_pos] = '\0';
                fprintf(stderr, "Error, línea %zu demasiado larga:  \"%s.. .\"\n", numLine, line);
                // CORREGIDO: Esperar hijos antes de salir
                esperarTodosLosHijos();
                free(buf);
                free(line);
                exit(EXIT_FAILURE);
            }

            line[line_pos++] = c;

            if (c == '\n')
            {
                line[line_pos] = '\0';
                char *trimmed = trim(line);
                if (*trimmed != '\0')
                {
                    LOG(LOG_LEVEL_INFO, "Procesando línea %zu: \"%s\"", numLine, trimmed);
                    int redireccion = comprobarRedireccion(line);
                    if (redireccion == -1)
                    {
                        esperarTodosLosHijos();
                        free(buf);
                        free(line);
                        exit(EXIT_FAILURE);
                    }

                    // CORREGIDO: Usar sigsuspend en lugar de pause para evitar race conditions
                    sigprocmask(SIG_BLOCK, &mask, &oldmask);
                    while(procesosActivos >= numProcs)
                    {
                        LOG(LOG_LEVEL_INFO, "Máximo de procesos (%d) alcanzado, esperando.. .", numProcs);
                        sigsuspend(&oldmask); // Reemplaza pause()
                    }

                    pid_t pid = fork();
                    if (pid == -1) 
                    {
                        perror("fork()");
                        sigprocmask(SIG_SETMASK, &oldmask, NULL);
                        esperarTodosLosHijos();
                        free(buf);
                        free(line);
                        exit(EXIT_FAILURE);
                    } 
                    else if (pid == 0)
                    {
                        // En el hijo, restaurar la máscara de señales y ejecutar la línea para mostrar el error en caso de fallo
                        signal(SIGCHLD, SIG_DFL);
                        sigprocmask(SIG_SETMASK, &oldmask, NULL);
                        ejecutarLinea(line, numLine);
                        exit(EXIT_SUCCESS);
                    } 
                    else 
                    {
                        procesosActivos++;
                        LOG(LOG_LEVEL_INFO, "Forked child %d for line %zu (procesosActivos=%d)", (int)pid, numLine, (int)procesosActivos);
                        sigprocmask(SIG_SETMASK, &oldmask, NULL);
                    }
                    numLine++;
                }
                line_pos = 0;
            }
        }
    }

    // Procesar última línea si no termina en \n
    if (line_pos > 0)
    {
        line[line_pos] = '\0';
        char *trimmed = trim(line);
        if (*trimmed != '\0')
        {
            LOG(LOG_LEVEL_INFO, "Procesando última línea %zu (sin \\n): \"%s\"", numLine, trimmed);
            int redireccion = comprobarRedireccion(line);
            if (redireccion == -1)
            {
                esperarTodosLosHijos();
                free(buf);
                free(line);
                exit(EXIT_FAILURE);
            }
            
            // CORREGIDO:  Usar sigsuspend
            sigprocmask(SIG_BLOCK, &mask, &oldmask);
            while(procesosActivos >= numProcs)
            {
                sigsuspend(&oldmask);
            }
            
            pid_t pid = fork();
            if (pid == -1) 
            {
                perror("fork()");
                sigprocmask(SIG_SETMASK, &oldmask, NULL);
                esperarTodosLosHijos();
                free(buf);
                free(line);
                exit(EXIT_FAILURE);
            } 
            else if (pid == 0)
            {
                signal(SIGCHLD, SIG_DFL);
                sigprocmask(SIG_SETMASK, &oldmask, NULL);
                ejecutarLinea(line, numLine);
                exit(EXIT_SUCCESS);
            } 
            else 
            {
                procesosActivos++;
                LOG(LOG_LEVEL_INFO, "Forked child %d for last line %zu (procesosActivos=%d)", (int)pid, numLine, (int)procesosActivos);
                sigprocmask(SIG_SETMASK, &oldmask, NULL);
            }
        }
    }

    // CORREGIDO: Esperar todos los hijos de forma ordenada
    esperarTodosLosHijos();

    if (num_read == -1)
    {
        perror("read()");
        free(buf);
        free(line);
        exit(EXIT_FAILURE);
    }

    LOG(LOG_LEVEL_INFO, "Finalizando programa.  Hijos terminados:  %d, Errores: %d", (int)hijos_terminados, (int)error_hijos);

    free(buf);
    free(line);
    
    //Salir con código de error si hubo errores en hijos
    return (error_hijos) ? EXIT_FAILURE : EXIT_SUCCESS;
}