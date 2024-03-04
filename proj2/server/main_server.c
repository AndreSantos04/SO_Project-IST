#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t clients_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct{
  char req_pipe[MAX_PIPE_NAME_SIZE];
  char resp_pipe[MAX_PIPE_NAME_SIZE];
} Client;

typedef struct clientList{
  Client* client;
  struct clientList* next;
} ClientList;

typedef struct {
  ClientList* head;
  ClientList* tail;
  size_t num_clients;
} BufferQueue;

BufferQueue* queue;

int client_waiting = 0;

void addClient(Client* client);

Client* popClient();

int usr1_signalled = 0;
int client_unavailable = 0;


void* threads_function(void* arg);

void handleSIGUSR1(int s);


int main(int argc, char* argv[]) {
  struct sigaction sa_usr1, sa_pipe;
  sa_usr1.sa_handler = &handleSIGUSR1;
  sa_usr1.sa_flags = 0;
  sigemptyset(&sa_usr1.sa_mask);

  sa_pipe.sa_handler = SIG_IGN;
  sa_pipe.sa_flags = 0;
  sigemptyset(&sa_pipe.sa_mask);

  printf("Server PID: %d\n", getpid());

  if(sigaction(SIGUSR1, &sa_usr1, NULL)==-1){
    lock_printf();
    fprintf(stderr, "[ERR]: sigaction failed: %s\n", strerror(errno));
    unlock_printf();
    return 1;
  }
  
  if(sigaction(SIGPIPE, &sa_pipe, NULL)==-1){
    lock_printf();
    fprintf(stderr, "[ERR]: sigaction failed: %s\n", strerror(errno));
    unlock_printf();
    return 1;
  }

  if((queue = (BufferQueue*)malloc(sizeof(BufferQueue)) )== NULL){
    lock_printf();
    fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
    unlock_printf();
    return 1;
  }
  queue->head = NULL;
  queue->tail = NULL;
  queue->num_clients = 0;


  if (argc < 2 || argc > 3) {
    lock_printf();
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    unlock_printf();
    return 1;
  }
  
  char* endptr;
  unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      lock_printf();
      fprintf(stderr, "Invalid delay value or value too large\n");
      unlock_printf();
      return 1;
    }

    state_access_delay_us = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_us)) {
    lock_printf();
    fprintf(stderr, "Failed to initialize EMS\n");
    unlock_printf();
    return 1;  
  }
  

  char *server_pipe_path = argv[1];
  if (unlink(server_pipe_path) == -1 && errno != ENOENT) {
    lock_printf();
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", server_pipe_path,
            strerror(errno));
    unlock_printf();
    return 1;
  }
  
  if(mkfifo(server_pipe_path, 0666) == -1){
    lock_printf();
    fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
    unlock_printf();
    return 1;
  }

  pthread_t threads[MAX_SESSION_COUNT];


  // Abrimos o fifo com read e write para que haja sempre um leitor passivo, de modo ao pipe não fechar desnecessariamente caso não hajam clientes conectados
  int server_fd = open(server_pipe_path, O_RDWR);

  if(server_fd ==-1){
    lock_printf();
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    unlock_printf();
    return 1;
  }

  for(int i=0; i<MAX_SESSION_COUNT; i++){

    int* arg = malloc(sizeof(int));
    if(arg == NULL){
      lock_printf();
      fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
      unlock_printf();
      return 1;
    }
    *arg = i;
    if(pthread_create(&threads[i], NULL, threads_function, (void*)arg) != 0){
      lock_printf();
      fprintf(stderr, "[ERR]: pthread_create failed: %s\n", strerror(errno));
      unlock_printf();
      return 1;
    }
  }

  size_t setup_session_size = 2*MAX_PIPE_NAME_SIZE*sizeof(char);
  char buf[setup_session_size];
  char OP_CODE;
  char req_pipe[MAX_PIPE_NAME_SIZE];
  char resp_pipe[MAX_PIPE_NAME_SIZE];

  //LOOP PARA RECEBER PEDIDOS PARA JUNTAR À QUEUE
  while(1){
    OP_CODE = '0';

    if(usr1_signalled){
      if(ems_show_all()){
        lock_printf();
        fprintf(stderr, "[ERR]: ems_show_all failed: %s\n", strerror(errno));
        unlock_printf();
      }

      usr1_signalled = 0;
    }
  
    pthread_mutex_lock(&queue_mutex);
    while(client_waiting >= MAX_CLIENTS_WAITING){
      pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    pthread_mutex_unlock(&queue_mutex);
    

    
    if(safe_read(server_fd, &OP_CODE, sizeof(char)) == -1){
      lock_printf();
      fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
      unlock_printf();
      continue;
    } 

    if (OP_CODE != '1'){ 
      //Caso a mensagem recebida não seja para iniciar uma sessão, ignora-a
      continue;
    }
    if(safe_read(server_fd, buf, setup_session_size) == -1){
      lock_printf();
      fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
      unlock_printf();
      continue;
    } 

    read_data(buf, req_pipe, MAX_PIPE_NAME_SIZE*sizeof(char));
    read_data(buf + MAX_PIPE_NAME_SIZE*sizeof(char), resp_pipe, MAX_PIPE_NAME_SIZE*sizeof(char));

    //Cria um cliente
    Client* client = malloc(sizeof(Client));
    if(client == NULL){
      lock_printf();
      fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
      unlock_printf();
      continue;
    }

    strcpy(client->req_pipe,req_pipe);
    strcpy(client->resp_pipe,resp_pipe);

    //Adiciona o cliente à queue
    pthread_mutex_lock(&queue_mutex);
    addClient(client);
    pthread_mutex_unlock(&queue_mutex);

    //Avisa as threads que há um novo cliente na queue
    pthread_mutex_lock(&clients_mutex);
    pthread_cond_signal(&clients_cond);
    pthread_mutex_unlock(&clients_mutex);


  }

  // A partir deste ponto os erros não devem terminar o servidor uma vez que não 
  // irão afetar o seu funcionamento até ao término do programa
  for(int i = 0; i < MAX_SESSION_COUNT; i++){
    if(pthread_join(threads[i], NULL) != 0){
      lock_printf();
      fprintf(stderr, "[ERR]: pthread_join failed: %s\n", strerror(errno));
      unlock_printf();    
    }
  }
  

  if(close(server_fd) == -1) {
    lock_printf();
    fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
    unlock_printf();
  }

  if(unlink(server_pipe_path) == -1){
    lock_printf();
    fprintf(stderr, "[ERR]: unlink failed: %s\n", strerror(errno));
    unlock_printf();
  }


  if(ems_terminate()){
    lock_printf();
    fprintf(stderr, "[ERR]: ems_terminate failed: %s\n", strerror(errno));
    unlock_printf();
    return 1;
  }

  return 0;
}

void* threads_function(void* arg){


  int session_id = *((int*)arg);
  sigset_t newMask;
  if(sigemptyset(&newMask) == -1){
    lock_printf();
    fprintf(stderr, "[ERR]: sigemptyset failed: %s\n", strerror(errno));
    unlock_printf();
    return NULL;
  }
  if(sigaddset(&newMask, SIGUSR1)==-1){
    lock_printf();
    fprintf(stderr, "[ERR]: sigaddset failed: %s\n", strerror(errno));
    unlock_printf();
    return NULL;
  }

  if(pthread_sigmask(SIG_BLOCK, &newMask, NULL)!=0){
    lock_printf();
    fprintf(stderr, "[ERR]: pthread_sigmask failed: %s\n", strerror(errno));
    unlock_printf();
    return NULL;
  }

  //LOOP PARA CONECTAR A UM NOVO CLIENTE E RECEBER PEDIDOS DELE
  while(1){
    // nas próximas linhas usamos o lock do mutex dos clients para poder
    // esperar caso não hajam clients e o lock da queue para podermos aceder
    // ao num_clients, este tivemos de usar do que uma vez de modo a impedir
    // esperas desnecessárias no wait
    pthread_mutex_lock(&clients_mutex);
    pthread_mutex_lock(&queue_mutex);
    while(queue->num_clients == 0){
      pthread_mutex_unlock(&queue_mutex);
      pthread_cond_wait(&clients_cond,&clients_mutex);
      pthread_mutex_lock(&queue_mutex);
    }

    //Avisa o main que foi retirado um cliente à queue
    Client* client = popClient();
    pthread_cond_signal(&queue_cond);

    pthread_mutex_unlock(&queue_mutex);
    pthread_mutex_unlock(&clients_mutex);


    char* req_pipe = client->req_pipe;
    char* resp_pipe = client->resp_pipe;

    int req_fd = open(req_pipe, O_RDONLY);
    if(req_fd == -1){
      lock_printf();
      fprintf(stderr, "[ERR]: open request pipe failed(server): %s\n", strerror(errno));
      unlock_printf();
      continue;
    }

    int resp_fd = open(resp_pipe, O_WRONLY);
    if(resp_fd == -1){
      lock_printf();
      fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
      unlock_printf();
      continue;
    }

    if(safe_write(resp_fd, &session_id, sizeof(int)) == -1){
      lock_printf();
      fprintf(stderr, "[ERR]: write to server pipe failed: %s\n", strerror(errno));
      unlock_printf();
      continue;
    }

    char OP_CODE;
    unsigned int event_id;
    size_t num_rows, num_cols;
    size_t num_seats;
    size_t num_events;
    size_t xs[MAX_RESERVATION_SIZE];
    size_t ys[MAX_RESERVATION_SIZE];
    int ret;
    void* ret_out;

    size_t buf_create_size = sizeof(unsigned int) + sizeof(size_t) + sizeof(size_t);
    size_t buf_reserve_size = sizeof(unsigned int) + sizeof(size_t) + 2*sizeof(size_t)*MAX_RESERVATION_SIZE;
    size_t buf_show_size = sizeof(unsigned int);
    size_t buf_OP_CODE_size = sizeof(char) + sizeof(int);

    char buf_create[buf_create_size];
    char buf_reserve[buf_reserve_size];
    char buf_show[buf_show_size];
    char buf_OP_CODE[buf_OP_CODE_size];


    int var = 1;
    //LOOP PARA RECEBER PEDIDOS DO CLIENTE
    while (var) {


      OP_CODE = '0';

      if(safe_read(req_fd, buf_OP_CODE, buf_OP_CODE_size) == -1){
        lock_printf();
        fprintf(stderr, "[ERR]: read OP_CODE from client failed: %s\n", strerror(errno));
        unlock_printf();
        break;
      }

      read_data(buf_OP_CODE, &OP_CODE, sizeof(char));
      read_data(buf_OP_CODE + sizeof(char), &session_id, sizeof(int));
      switch(OP_CODE){
        case '1':
          // Se for uma mensagem para iniciar uma sessão, ignora-a
          break;
        case '2': //QUIT
          if(close(req_fd) ==-1){
            lock_printf();
            fprintf(stderr, "[ERR]: close request pipe failed: %s\n", strerror(errno));
            unlock_printf();

          }
          if(close(resp_fd) ==-1){
            lock_printf();
            fprintf(stderr, "[ERR]: close response pipe failed: %s\n", strerror(errno));
            unlock_printf();
          }
          var = 0;
          break;
        case '3': //CREATE

          memset(buf_create, 0, buf_create_size);         
          if(safe_read(req_fd, buf_create, buf_create_size) == -1){
            lock_printf();
            fprintf(stderr, "[ERR]: read create from client failed: %s\n", strerror(errno));
            unlock_printf();
            var = 0;
            break;
          }

          read_data(buf_create, &event_id, sizeof(unsigned int));

          read_data(buf_create + sizeof(unsigned int), &num_rows, sizeof(size_t));

          read_data(buf_create + sizeof(unsigned int) + sizeof(size_t), &num_cols, sizeof(size_t));

          ret = ems_create(event_id, num_rows, num_cols);

          if(safe_write(resp_fd, &ret, sizeof(unsigned int)) ==-1){
            lock_printf();
            fprintf(stderr, "[ERR]: write to server pipe failed: %s\n", strerror(errno));
            unlock_printf();
            var = 0;
          }


          break;
        case '4': //RESERVE
          memset(buf_reserve, 0, buf_reserve_size);      
          if(safe_read(req_fd, buf_reserve, buf_reserve_size) == -1){
            lock_printf();
            fprintf(stderr, "[ERR]: read reserve from client failed: %s\n", strerror(errno));
            unlock_printf();
            var = 0;
            break;
          }

          read_data(buf_reserve , &event_id, sizeof(unsigned int));

          read_data(buf_reserve + sizeof(unsigned int), &num_seats, sizeof(size_t));

          read_data(buf_reserve + sizeof(unsigned int) + sizeof(size_t), xs, sizeof(size_t)*num_seats);

          read_data(buf_reserve + sizeof(unsigned int) + sizeof(size_t) + sizeof(size_t)*num_seats, ys, sizeof(size_t)*num_seats);

          ret = ems_reserve(event_id, num_seats, xs, ys);
          if(safe_write(resp_fd, &ret, sizeof(int)) == -1){
            lock_printf();
            fprintf(stderr, "[ERR]: write to server pipe failed: %s\n", strerror(errno));
            unlock_printf();
            var = 0;
          }
          break;
        case '5': //SHOW_EVENT
        
          memset(buf_show, 0, buf_show_size);
          if(safe_read(req_fd, buf_show, buf_show_size) == -1){
            lock_printf();
            fprintf(stderr, "[ERR]: read show from client failed: %s\n", strerror(errno));
            unlock_printf();
            var = 0;
            break;
          }

          read_data(buf_show, &event_id, sizeof(unsigned int));

          ret_out = ems_show(event_id);
          if(ret_out == NULL){
            lock_printf();
            fprintf(stderr, "[ERR]: ems_show failed: %s\n", strerror(errno));
            unlock_printf();
            var = 0;
            break;
          }
          int ret_show = *((int*)ret_out);

          if(ret_show){

            if(safe_write(resp_fd, ret_out, sizeof(int)) == -1){ 
              lock_printf();
              fprintf(stderr, "[ERR]: write to server pipe failed: %s \n", strerror(errno));
              unlock_printf();
              var = 0;
            }
          }
          else{ 
            read_data(ret_out + sizeof(int), &num_rows, sizeof(size_t));
            read_data(ret_out + sizeof(int) + sizeof(size_t), &num_cols, sizeof(size_t));
            if(safe_write(resp_fd, ret_out, sizeof(int) + 2*sizeof(size_t)+ sizeof(unsigned int)*num_rows*num_cols) == -1){ 
              lock_printf();
              fprintf(stderr, "[ERR]: write to server pipe failed: %s \n", strerror(errno));
              unlock_printf();
              var = 0;
            }
          }
          free(ret_out);
          break;
        case '6': //LIST_EVENT
          
          ret_out = ems_list_events();
          if(ret_out == NULL){
            lock_printf();
            fprintf(stderr, "[ERR]: ems_list_events failed: %s\n", strerror(errno));
            unlock_printf();
            var = 0;
            break;
          }
          int ret_list = *((int*)ret_out);
          if(ret_list){
            if(safe_write(resp_fd, ret_out, sizeof(int)) == -1){ 
              lock_printf();
              fprintf(stderr, "[ERR]: write to server pipe failed: %s \n", strerror(errno));
              unlock_printf();
              var= 0;
            }
          }
          else{
            read_data(ret_out + sizeof(int), &num_events, sizeof(size_t));

            if(safe_write(resp_fd, ret_out,sizeof(int) + sizeof(size_t) + sizeof(unsigned int)*num_events) == -1){ 
              lock_printf();
              fprintf(stderr, "[ERR]: write to server pipe failed: %s\n", strerror(errno));
              unlock_printf();
              var = 0;
            }
          }
          free(ret_out);
          break;
        default:

          break;
      }
    }
  }
  free(arg);
  return NULL;
  //LIBERTA A SESSAO DO CLIENTE E VOLTA A ESPERAR POR UM NOVO CLIENTE
}

void handleSIGUSR1(int s){
  if(s != SIGUSR1)
    return;
  usr1_signalled = 1;
}

void addClient(Client* client){
  ClientList* new_client = malloc(sizeof(ClientList));
  new_client->client = client;
  new_client->next = NULL;
  if(queue->head==NULL){
    queue->tail=new_client;
    queue->head=new_client;
  }
  else{
  queue->tail->next = new_client;
  queue->tail = new_client;
  }
  queue->num_clients++;
}

Client* popClient(){
  Client* client = queue->head->client;
  if(queue->head==NULL){
    return NULL;
  }
  queue->head = queue->head->next;
  queue->num_clients--;
  return client;
}