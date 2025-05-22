#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
/* #include <nuttx/serial/tioctl.h> */
/* #include <nuttx/fs/ioctl.h> */
#include <errno.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include "netutils/cJSON.h"


#define BUFFER_SIZE 1024
#define LINE_BUFFER_SIZE 4096
#define MAX_SAFE_PACKET 120
#define MAX_SAFE_CHUNK 120
#define CMD_OUTPUT_SIZE 2048
#define MAX_CMD_LENGTH 256
#define SERIAL_DEVICE "/dev/ttyACM0"
#define MAX_QUEUE_SIZE 10

volatile sig_atomic_t g_is_running = 1;

typedef struct {
  int code;           // status code
  char *msg;          // status message
} json_status_t;

typedef struct {
  long long androidtime;  // Android timestamp
  long long runtime;      // execution time
} json_timestamp_t;

typedef struct {
  char *cmd;              // command
  char type[5];           // message type (res/req/poll)
  uint interval;          // polling (type == poll)
  uint id;                // command ID
  uint priority;         // priority(0:default)
  json_status_t status;   // command return status
  cJSON *data;            // output of command
  cJSON *config;          // command options
  json_timestamp_t timestamp; // unixtime, runtime
  char version[5];        // json version
} json_message_t;

typedef struct {
  json_message_t **items;       // コマンドアイテムの配列
  int capacity;                 // キューの容量
  int size;                     // 現在のサイズ
  int head;                     // キューの先頭インデックス
  int tail;                     // キューの末尾インデックス
  pthread_mutex_t mutex;        // 排他制御用ミューテックス
  pthread_cond_t not_empty;     // キューが空でない条件変数
  pthread_cond_t not_full;      // キューが満杯でない条件変数
} spresense_task_queue_t;

// ワーカースレッド引数構造体
typedef struct {
  spresense_task_queue_t *queue; // task queue
  int serial_fd;                 // dev/ttyACM0
} worker_thread_args_t;

void *spresense_worker_thread(void *arg);
long long current_time_ms(void);
int execute_command(const char *cmd, char **output);
/* int execute_command(const char *cmd, char *output, size_t output_size); */
void process_json(const char *json_str, int serial_fd, spresense_task_queue_t *queue);
json_message_t *parse_json_message(const char *json_str);
json_message_t *create_json_message(void);
void free_json_message(json_message_t *msg);
char *json_message_to_string(const json_message_t *msg);
json_message_t *copy_json_message(const json_message_t *src);
spresense_task_queue_t *spresense_task_queue_initialized(int capacity);
int spresense_task_queue_add(spresense_task_queue_t *queue, json_message_t *msg);
void spresense_task_queue_free(spresense_task_queue_t *queue);
json_message_t *spresense_task_queue_pop(spresense_task_queue_t *queue);
void signal_handler(int sig);
void signal_worker_shutdown(spresense_task_queue_t *queue);

void *spresense_worker_thread(void *arg) {
  worker_thread_args_t *args = (worker_thread_args_t *)arg;
  spresense_task_queue_t *queue = args->queue;
  int serial_fd = args->serial_fd;

  printf("spresense_worker_thread: started\n");

  while (g_is_running) {
    json_message_t *msg = spresense_task_queue_pop(queue);
    if (msg == NULL) {
      // queue is empty or Ctrl+C
      usleep(1000000);  // wait 10ms
      printf("spresense_worker_thread: queue is NULL\n");
      continue;
    }

    const char *cmd = msg->cmd;
    if (cmd == NULL || strlen(cmd) == 0) {
      printf("spresense_worker_thread: Empty command received, skipping\n");
      free_json_message(msg);
      continue;
    }
    printf("spresense_worker_thread: executing command: %s\n", msg->cmd);

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    char *output = NULL;
    int status = execute_command(msg->cmd, &output);
    /* int status = 0; */
    /* char *output = strdup("{\"status\":{\"code\": 1, \"msg\": \"test code\" }, \"data\":{\"x\":1, \"y\":2, \"z\": 3}, \"config\":{\"c\":\"1\", \"d\":2} }"); */
    usleep(10000000);

    if (output == NULL) {
      printf("spresense_worker_thread: Failed to allocate memory for output\n");
      free_json_message(msg);
      continue;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    long runtime_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 +
      (end_time.tv_nsec - start_time.tv_nsec) / 1000000;

    printf("spresense_worker_thread: Command executed with status %d in %ld ms\n", status, runtime_ms);

    json_message_t *response = copy_json_message(msg);
    if (response != NULL) {
      // type: req->res
      if (response->type != NULL && strcmp(response->type, "req") == 0) {
        strncpy(response->type, "res", sizeof(response->type) - 1);
        response->type[sizeof(response->type) - 1] = '\0';
      }

      // output->status, data, config
      cJSON *output_json = NULL;
      if (output[0] != '\0') {
        output_json = cJSON_Parse(output);
      }
      if (output_json != NULL) {
        // If output is JSON, update status, data and config
        // update status
        cJSON *status_obj = cJSON_GetObjectItemCaseSensitive(output_json, "status");
        if (cJSON_IsObject(status_obj)) {
          cJSON *code = cJSON_GetObjectItemCaseSensitive(status_obj, "code");
          if (cJSON_IsNumber(code)) {
            response->status.code = code->valueint;
          } else {
            response->status.code = 0;
          }
          cJSON *msg_obj = cJSON_GetObjectItemCaseSensitive(status_obj, "msg");
          if (cJSON_IsString(msg_obj) && msg_obj->valuestring != NULL) {
            if (response->status.msg != NULL) {
              free(response->status.msg);
            }
            response->status.msg = strdup(msg_obj->valuestring);
          }
        } else {
          // status is None, use return value of pclose()
          response->status.code = 0;
          if (response->status.msg != NULL) {
            free(response->status.msg);
          }
          response->status.msg = strdup("OK");
        }

        // update data
        cJSON *data_obj = cJSON_GetObjectItemCaseSensitive(output_json, "data");
        if (cJSON_IsObject(data_obj)) {
          if (response->data != NULL) {
            cJSON_Delete(response->data);
          }
          response->data = cJSON_Duplicate(data_obj, 1);
        } else {
          // data is None, use all output
          if (response->data != NULL) {
            cJSON_Delete(response->data);
          }
          response->data = cJSON_CreateObject();
          cJSON_AddStringToObject(response->data, "output", output);
        }

        // update config
        cJSON *config_obj = cJSON_GetObjectItemCaseSensitive(output_json, "config");
        if (cJSON_IsObject(config_obj)) {
          if (response->config != NULL) {
            cJSON_Delete(response->config);
          }
          response->config = cJSON_Duplicate(config_obj, 1);
        }
        cJSON_Delete(output_json);
      } else {
        // output is not JSON
        // update status
        response->status.code = 0;
        if (response->status.msg != NULL) {
          free(response->status.msg);
        }
        response->status.msg = strdup("OK");

        // update data
        if (response->data != NULL) {
          cJSON_Delete(response->data);
        }
        response->data = cJSON_CreateObject();
        cJSON_AddStringToObject(response->data, "output", output);
      }
      free(output);
      // update runtime
      response->timestamp.runtime = runtime_ms;

      char *json_str = json_message_to_string(response);
      if (json_str != NULL) {
        size_t len = strlen(json_str);
        char *final_response = malloc(len + 2);
        if (final_response != NULL) {
          strcpy(final_response, json_str);
          final_response[len] = '\n';
          final_response[len + 1] = '\0';

          // transmit response
          write(serial_fd, final_response, len + 1);
          printf("spresense_worker_thread: Response sent: %s\n", json_str);

          free(final_response);
        }
        free(json_str);
      }
      free_json_message(response);
    }
    free_json_message(msg);
  }

  printf("spresense_worker_thread: exiting\n");
  return NULL;
}

long long current_time_ms() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)(tv.tv_sec) * 1000 + (tv.tv_usec / 1000);
}

int execute_command(const char *cmd, char **output) {
  FILE *fp;
  char buffer[128];
  size_t total_size = 0;
  size_t buffer_size = 0;

  printf("execute_command: '%s'\n", cmd);

  fp = popen(cmd, "r");
  if (fp == NULL) {
    printf("execute_command: Failed to popen\n");
    return -1;
  }

  *output = NULL;
  printf("execute_command: initialize output buffer\n");

  while (fgets(buffer, sizeof(buffer), fp) != NULL) {
    size_t len = strlen(buffer);

    // spread buffer size
    if (total_size + len + 1 > buffer_size) {
      buffer_size = total_size + len + 128;
      printf("execute_command: spread buffer size: %zu bytes\n", buffer_size);
      char *new_output = realloc(*output, buffer_size);
      if (new_output == NULL) {
        free(*output);
        *output = NULL;
        pclose(fp);
        return -2;
      }
      *output = new_output;
    }

    memcpy(*output + total_size, buffer, len);
    total_size += len;
    (*output)[total_size] = '\0';
  }
  printf("execute_command: output is %zu bytes\n", total_size);

  int status = pclose(fp);
  printf("execute_command: pclose status: %d\n", status);

  // output is None
  if (*output == NULL) {
    *output = malloc(1);
    if (*output == NULL) {
      return -2;
    }
    free(*output);

    char no_output_json[256];
    snprintf(no_output_json, sizeof(no_output_json),
             "{\"status\":{\"code\":-1, \"msg\":\"%s output not found\"}}", cmd);
    *output = strdup(no_output_json);
  }

  return status;
}

void process_json(const char *json_str, int serial_fd, spresense_task_queue_t *queue) {
  printf("Processing JSON: %s\n", json_str);
  json_message_t *request = parse_json_message(json_str);
  if (request == NULL) {
    printf("process_json: Invalid JSON format\n");

    json_message_t *error_response = create_json_message();
    if (error_response != NULL) {
      strncpy(error_response->type, "res", sizeof(error_response->type) - 1);
      error_response->type[sizeof(error_response->type) - 1] = '\0';
      error_response->status.code = -1;
      error_response->status.msg = strdup("Invalid JSON format");

      char *error_json = json_message_to_string(error_response);
      if (error_json != NULL) {
        size_t len = strlen(error_json);
        write(serial_fd, error_json, len);
        free(error_json);
      }

      free_json_message(error_response);
    }
    return;
  }


  // JSON must have cmd key
  if (request->cmd == NULL || strlen(request->cmd) == 0) {
    printf("process_json: No command specified in JSON\n");

    json_message_t *error_response = create_json_message();
    if (error_response != NULL) {
      strncpy(error_response->type, "res", sizeof(error_response->type) - 1);
      error_response->type[sizeof(error_response->type) - 1] = '\0';
      error_response->status.code = -1;
      error_response->status.msg = strdup("No command specified");

      char *error_json = json_message_to_string(error_response);
      if (error_json != NULL) {
        size_t len = strlen(error_json);
        write(serial_fd, error_json, len);
        free(error_json);
      }

      free_json_message(error_response);
    }
    free_json_message(request);
    return;
  }

  // add queue
  if (spresense_task_queue_add(queue, request) != 0) {
    printf("process_json: Failed to add task queue\n");
    free_json_message(request);
    return;
  }

  printf("process_json: Command '%s' added to queue\n", request->cmd);
}

json_message_t *parse_json_message(const char *json_str) {
  if (json_str == NULL) {
    return NULL;
  }
  cJSON *json = cJSON_Parse(json_str);
  if (json == NULL) {
    printf("parse_json_message: JSON parse error: %s\n", cJSON_GetErrorPtr());
    return NULL;
  }

  json_message_t *msg = create_json_message();
  if (msg == NULL) {
    cJSON_Delete(json);
    return NULL;
  }

  // parameters check
  cJSON *cmd = cJSON_GetObjectItemCaseSensitive(json, "cmd");
  if (cJSON_IsString(cmd) && cmd->valuestring != NULL) {
    msg->cmd = strdup(cmd->valuestring);
  }
  cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "type");
  if (cJSON_IsString(type) && type->valuestring != NULL) {
    strncpy(msg->type, type->valuestring, sizeof(msg->type) - 1);
    msg->type[sizeof(msg->type) - 1] = '\0';
  }
  cJSON *interval = cJSON_GetObjectItemCaseSensitive(json, "interval");
  if (cJSON_IsNumber(interval)) {
    msg->interval = (uint)interval->valueint;
  }
  cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");
  if (cJSON_IsNumber(id)) {
    msg->id = (uint)id->valueint;
  }
  cJSON *priority = cJSON_GetObjectItemCaseSensitive(json, "priority");
  if (cJSON_IsNumber(priority)) {
    msg->priority = (uint)priority->valueint;
  }
  cJSON *config = cJSON_GetObjectItemCaseSensitive(json, "config");
  if (cJSON_IsObject(config)) {
    msg->config = cJSON_Duplicate(config, 1);
  }
  cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
  if (cJSON_IsObject(timestamp)) {
    cJSON *androidtime = cJSON_GetObjectItemCaseSensitive(timestamp, "androidtime");
    if (cJSON_IsNumber(androidtime)) {
      msg->timestamp.androidtime = androidtime->valueint;
    }
  }
  cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
  if (cJSON_IsString(version) && version->valuestring != NULL) {
    strncpy(msg->version, version->valuestring, sizeof(msg->version) - 1);
    msg->version[sizeof(msg->version) - 1] = '\0';
  }

  cJSON_Delete(json);

  return msg;
}

json_message_t *create_json_message(void) {
  json_message_t *msg = (json_message_t *)malloc(sizeof(json_message_t));
  if (msg == NULL) {
    return NULL;
  }
  // pointer->NULL, int->0;
  memset(msg, 0, sizeof(json_message_t));
  // default parameters
  strcpy(msg->type, "req");
  strcpy(msg->version, "1.0");
  return msg;
}

void free_json_message(json_message_t *msg) {
  if (msg == NULL) {
    return;
  }

  if (msg->cmd != NULL) free(msg->cmd);
  if (msg->status.msg != NULL) free(msg->status.msg);

  if (msg->data != NULL) cJSON_Delete(msg->data);
  if (msg->config != NULL) cJSON_Delete(msg->config);

  free(msg);
}

char *json_message_to_string(const json_message_t *msg) {
  if (msg == NULL) {
    return NULL;
  }

  cJSON *json = cJSON_CreateObject();
  if (json == NULL) {
    return NULL;
  }

  if (msg->cmd != NULL) {
    cJSON_AddStringToObject(json, "cmd", msg->cmd);
  }

  cJSON_AddStringToObject(json, "type", msg->type);

  if (msg->interval > 0) {
    cJSON_AddNumberToObject(json, "interval", msg->interval);
  }

  cJSON_AddNumberToObject(json, "id", msg->id);

  if (msg->priority > 0) {
    cJSON_AddNumberToObject(json, "priority", msg->priority);
  }

  cJSON *status = cJSON_CreateObject();
  cJSON_AddNumberToObject(status, "code", msg->status.code);
  if (msg->status.msg != NULL) {
    cJSON_AddStringToObject(status, "msg", msg->status.msg);
  } else {
    cJSON_AddStringToObject(status, "msg", msg->status.code == 0 ? "OK" : "Error");
  }
  cJSON_AddItemToObject(json, "status", status);

  if (msg->data != NULL) {
    cJSON_AddItemToObject(json, "data", cJSON_Duplicate(msg->data, 1));
  } else {
    cJSON_AddItemToObject(json, "data", cJSON_CreateObject());
  }

  if (msg->config != NULL) {
    cJSON_AddItemToObject(json, "config", cJSON_Duplicate(msg->config, 1));
  } else {
    cJSON_AddItemToObject(json, "config", cJSON_CreateObject());
  }

  cJSON *timestamp = cJSON_CreateObject();
  cJSON_AddNumberToObject(timestamp, "androidtime", msg->timestamp.androidtime);
  cJSON_AddNumberToObject(timestamp, "runtime", msg->timestamp.runtime);
  cJSON_AddItemToObject(json, "timestamp", timestamp);

  cJSON_AddStringToObject(json, "version", msg->version);

  char *json_str = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);

  return json_str;
}

json_message_t *copy_json_message(const json_message_t *src) {
  if (src == NULL) {
    return NULL;
  }

  json_message_t *dst = create_json_message();
  if (dst == NULL) {
    return NULL;
  }

  if (src->cmd != NULL) dst->cmd = strdup(src->cmd);

  strncpy(dst->type, src->type, sizeof(dst->type) - 1);
  dst->type[sizeof(dst->type) - 1] = '\0';
  strncpy(dst->version, src->version, sizeof(dst->version) - 1);
  dst->version[sizeof(dst->version) - 1] = '\0';

  dst->interval = src->interval;
  dst->id = src->id;
  dst->priority = src->priority;
  dst->status.code = src->status.code;
  dst->timestamp.androidtime = src->timestamp.androidtime;
  dst->timestamp.runtime = src->timestamp.runtime;

  if (src->status.msg != NULL) dst->status.msg = strdup(src->status.msg);

  if (src->data != NULL) dst->data = cJSON_Duplicate(src->data, 1);
  if (src->config != NULL) dst->config = cJSON_Duplicate(src->config, 1);

  return dst;
}

spresense_task_queue_t *spresense_task_queue_initialized(int capacity) {
  if (capacity <= 0) {
    capacity = MAX_QUEUE_SIZE;
  }

  spresense_task_queue_t *queue = (spresense_task_queue_t *)malloc(sizeof(spresense_task_queue_t));
  if (queue == NULL) {
    return NULL;
  }

  queue->items = (json_message_t **)malloc(sizeof(json_message_t *) * capacity);
  if (queue->items == NULL) {
    printf("spresense_task_queue_initialized: Failed to allocate memory for queue items\n");
    free(queue);
    return NULL;
  }

  if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
    printf("spresense_task_queue_initialized: Failed to initialize mutex\n");
    free(queue->items);
    free(queue);
    return NULL;
  }

  queue->capacity = capacity;
  queue->size = 0;
  queue->head = 0;
  queue->tail = 0;

  if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
    printf("spresense_task_queue_initialized: Failed to initialize condition variable (not_empty)\n");
    pthread_mutex_destroy(&queue->mutex);
    free(queue->items);
    free(queue);
    return NULL;
  }

  if (pthread_cond_init(&queue->not_full, NULL) != 0) {
    printf("spresense_task_queue_initialized: Failed to initialize condition variable (not_full)\n");
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
    free(queue->items);
    free(queue);
    return NULL;
  }

  printf("spresense_task_queue_initialized: Task queue created with capacity %d\n", capacity);
  return queue;
}

int spresense_task_queue_add(spresense_task_queue_t *queue, json_message_t *msg) {
  if (queue == NULL || msg == NULL) {
    return -1;
  }

  pthread_mutex_lock(&queue->mutex);

  while (queue->size >= queue->capacity && g_is_running) {
    printf("spresense_task_queue_add: Queue is full (%d/%d), waiting...\n",
           queue->size, queue->capacity);
    pthread_cond_wait(&queue->not_full, &queue->mutex);
  }

  if (!g_is_running) {
    pthread_mutex_unlock(&queue->mutex);
    return -1;
  }

  queue->items[queue->tail] = msg;
  queue->tail = (queue->tail + 1) % queue->capacity;
  queue->size++;

  printf("spresense_task_queue_add: Task added to queue (size: %d/%d)\n",
         queue->size, queue->capacity);

  pthread_cond_signal(&queue->not_empty);

  pthread_mutex_unlock(&queue->mutex);
  return 0;
}

void spresense_task_queue_free(spresense_task_queue_t *queue) {
  if (queue == NULL) {
    return;
  }

  pthread_mutex_lock(&queue->mutex);

  for (int i = 0; i < queue->size; i++) {
    int index = (queue->head + i) % queue->capacity;
    free_json_message(queue->items[index]);
    queue->items[index] = NULL;
  }

  pthread_cond_destroy(&queue->not_empty);
  pthread_cond_destroy(&queue->not_full);
  pthread_mutex_destroy(&queue->mutex);

  free(queue->items);
  free(queue);
  printf("spresense_task_queue_free: Task queue destroyed\n");
}

json_message_t *spresense_task_queue_pop(spresense_task_queue_t *queue) {
  if (queue == NULL) {
    return NULL;
  }

  printf("spresense_task_queue_pop: start (size: %d/%d)\n", queue->size, queue->capacity);
  pthread_mutex_lock(&queue->mutex);

  while (queue->size == 0 && g_is_running) {
    pthread_cond_wait(&queue->not_empty, &queue->mutex);
  }

  if (!g_is_running || queue->size == 0) {
    pthread_mutex_unlock(&queue->mutex);
    return NULL;
  }

  json_message_t *msg = queue->items[queue->head];
  queue->head = (queue->head + 1) % queue->capacity;
  queue->size--;


  pthread_cond_signal(&queue->not_full);
  pthread_mutex_unlock(&queue->mutex);
  return msg;
}

void signal_handler(int sig) {
  g_is_running = 0;
  printf("\nsignal_handler: Received signal %d, shutting down...\n", sig);
}

void signal_worker_shutdown(spresense_task_queue_t *queue) {
  if (queue == NULL) {
    return;
  }

  json_message_t *shutdown_msg = create_json_message();
  if (shutdown_msg == NULL) {
    printf("signal_worker_shutdown: Failed to create shutdown message\n");
    return;
  }
  shutdown_msg->cmd = strdup("__SHUTDOWN__");
  snprintf(shutdown_msg->type, sizeof(shutdown_msg->type), "%s", "res");
  shutdown_msg->id = 0;
  if (spresense_task_queue_add(queue, shutdown_msg) != 0) {
    printf("signal_worker_shutdown: Failed to add queue shutdown message\n");
    free_json_message(shutdown_msg);

    pthread_mutex_lock(&queue->mutex);
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
  }

  printf("signal_worker_shutdown: Shutdown signal sent to worker thread\n");
}

int main(int argc, FAR char *argv[])
{
  int serial_fd;
  char receive[BUFFER_SIZE];
  int bytes_read;

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  serial_fd = open(SERIAL_DEVICE, O_RDWR | O_NOCTTY);
  if (serial_fd < 0) {
    printf("main: Failed to open %s: %s\n", SERIAL_DEVICE, strerror(errno));
    return -1;
  }
  printf("%s open\n", SERIAL_DEVICE);

  spresense_task_queue_t *task_queue = spresense_task_queue_initialized(MAX_QUEUE_SIZE);
  if (task_queue == NULL) {
    printf("main: Failed to initialize command queue\n");
    close(serial_fd);
    return -1;
  }

  pthread_t worker_thread_id;
  worker_thread_args_t worker_args = {
    .queue = task_queue,
    .serial_fd = serial_fd
  };
  int thread_result = pthread_create(&worker_thread_id, NULL, spresense_worker_thread, &worker_args);
  if (thread_result != 0) {
    printf("main: Failed to create worker thread: %s\n", strerror(thread_result));
    close(serial_fd);
    spresense_task_queue_free(task_queue);
    return -1;
  }

  printf("main: Serial controller started. Maximum safe packet size: %d bytes\n", MAX_SAFE_PACKET);

  while (g_is_running) {
    bytes_read = read(serial_fd, receive, BUFFER_SIZE - 1);
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      printf("main: Failed to read from serial port: %s (%d)\n", strerror(errno), errno);
      usleep(100000);
      continue;
    } else if (bytes_read > 0) {

      receive[bytes_read] = '\0';
      printf("main: Data %s (%d bytes)\n", receive, bytes_read);

      char *line_start = receive;
      char *line_end;

      while ((line_end = strchr(line_start, '\n')) != NULL) {
        *line_end = '\0';
        printf("main: DataLine %s\n", line_start);
        if (strlen(line_start) > 0) {
          process_json(line_start, serial_fd, task_queue);
        }
        line_start = line_end + 1;
      }

      if (*line_start != '\0') {
        printf("main: cannot find delimiter %s\n", line_start);
        // 必要に応じて次回のために保存
      }
      usleep(10000);
    } else {
      usleep(50000);
    }
  }
  printf("main: Shutting down serial controller...\n");
  close(serial_fd);

  signal_worker_shutdown(task_queue);
  pthread_join(worker_thread_id, NULL);

  spresense_task_queue_free(task_queue);

  printf("main: Serial controller shutdown complete\n");

  return 0;
}
