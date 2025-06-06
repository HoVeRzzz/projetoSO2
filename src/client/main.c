#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "parser.h"
#include "src/client/api.h"
#include "src/common/constants.h"
#include "src/common/io.h"

int interrompido = 0;

void *notification_thread(void *arg) {
  int notif_pipe = *(int *)arg;
  char buffer[2 * (MAX_STRING_SIZE+1)];
  while (1) {
    // Ler notificação do pipe
    int result = read_all(notif_pipe, buffer, sizeof(buffer), NULL);
    if (result <= 0) {
      interrompido = 1;
      kvs_end();
      fprintf(stderr,"Failed to read notification");
      pthread_exit(NULL);
    }
    // Extrair chave e valor da notificação
    char key[(MAX_STRING_SIZE+1)], value[(MAX_STRING_SIZE+1)];
    strncpy(key, buffer, MAX_STRING_SIZE);
    key[MAX_STRING_SIZE] = '\0';
    strncpy(value, buffer + (MAX_STRING_SIZE+1), MAX_STRING_SIZE);
    value[MAX_STRING_SIZE] = '\0';
    // Imprimir chave e valor
    printf("(%s,%s)\n", key, value);
  }

  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <client_unique_id> <register_pipe_path>\n",
            argv[0]);
    return 1;
  }

  char req_pipe_path[MAX_PIPE_PATH_LENGTH];
  char resp_pipe_path[MAX_PIPE_PATH_LENGTH];
  char notif_pipe_path[MAX_PIPE_PATH_LENGTH];

  // Construir caminhos dos pipes
  snprintf(req_pipe_path, MAX_PIPE_PATH_LENGTH, "/tmp/req_%s", argv[1]);
  snprintf(resp_pipe_path, MAX_PIPE_PATH_LENGTH, "/tmp/resp_%s", argv[1]);
  snprintf(notif_pipe_path, MAX_PIPE_PATH_LENGTH, "/tmp/notif_%s", argv[1]);

  char keys[MAX_NUMBER_SUB][MAX_STRING_SIZE] = {0};
  unsigned int delay_ms;
  size_t num;

  int notif_pipe;
  // Conectar ao servidor
  int response_code = kvs_connect(req_pipe_path, resp_pipe_path, argv[2], notif_pipe_path, &notif_pipe);
  if (response_code != 0) {
    fprintf(stderr, "Failed to connect to the server\n");
    return 1;
  }
  
  // Criar thread para notificações
  pthread_t notif_thread;
  if (pthread_create(&notif_thread, NULL, notification_thread, &notif_pipe) != 0) {
    perror("Failed to create notification thread");
    return 1;
  }

  while (1) {
    if (interrompido) {
      return 1;
    }
    switch (get_next(STDIN_FILENO)) {
    case CMD_DISCONNECT:
        // Desconectar do servidor
        response_code = kvs_disconnect();
        if (response_code != 0) {
            fprintf(stderr, "Failed to disconnect to the server\n");
            return 1;
        }
        pthread_cancel(notif_thread);
        pthread_join(notif_thread, NULL);
        return 0;

    case CMD_SUBSCRIBE:
        // Processar comando de subscrição
        num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
        if (num == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
        }
        if (kvs_subscribe(keys[0])) {
            fprintf(stderr, "Command subscribe failed\n");
        }
        break;

    case CMD_UNSUBSCRIBE:
        // Processar comando de cancelamento de subscrição
        num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
        if (num == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
        }
        if (kvs_unsubscribe(keys[0])) {
            fprintf(stderr, "Command unsubscribe failed\n");
        }
        break;

    case CMD_DELAY:
        // Processar comando de atraso
        if (parse_delay(STDIN_FILENO, &delay_ms) == -1) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
        }
        if (delay_ms > 0) {
            printf("Waiting...\n");
            delay(delay_ms);
        }
        break;

    case CMD_INVALID:
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        break;

    case CMD_EMPTY:
        break;

    case EOC:
        // input should end in a disconnect, or it will loop here forever
        break;
    }
  }
}
