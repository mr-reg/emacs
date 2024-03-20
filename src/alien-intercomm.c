#include "alien-intercomm.h"
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "lisp.h"
#include <sys/resource.h>
#include <signal.h>

# define ALIEN_BACKTRACE_LIMIT 500
# define BACKTRACE_STR_SIZE 100000
char *backtrace_str[BACKTRACE_STR_SIZE];
typedef struct
{
  char type;
  long int_data;
  char *char_data;
  double float_data;
} alien_data_struct;
pthread_mutex_t input_mutex, output_mutex;
pthread_cond_t input_cond, output_cond;
bool intercomm_active = false;
long message_counter = 0;

typedef struct
{
  long message_id;
  long alien_input_length;
  alien_data_struct *alien_input_array;
  long alien_output_length;
  alien_data_struct *alien_output_array;
} intercomm_session_struct;


char *
get_alien_backtrace ()
{
  void *buffer[ALIEN_BACKTRACE_LIMIT + 1];
  int npointers;
  npointers = backtrace (buffer, ALIEN_BACKTRACE_LIMIT + 1);

  backtrace_str[0] = 0;
  FILE *stream = fmemopen (backtrace_str, BACKTRACE_STR_SIZE, "w");
  if (npointers)
    {
      fputs ("Backtrace:\n", stream);
      char** symbols = backtrace_symbols (buffer, npointers);
      for (int frame = 0; frame < npointers; frame++)
	{
	  fputs (symbols[frame], stream);
	  fputs ("\n", stream);
	}
      free(symbols);
    }
  fclose (stream);
  return (char*)backtrace_str;
}

void
init_alien_intercomm ()
{
  signal(SIGXCPU, SIG_IGN);

  /* struct rlimit limit; */
  /* getrlimit(RLIMIT_CPU, &limit); */
  /* printf("cpu limit = %ld, max = %ld\n", limit.rlim_cur, limit.rlim_max); */
  /* exit(0); */
  if (! (sizeof (EMACS_INT) == sizeof (long)))
    {
      printf("EMACS_INT size is unexpected\n");
      emacs_abort();
    }
}

void
lock_input_mutex ()
{
    pthread_mutex_lock(&input_mutex);
}

void
unlock_input_mutex ()
{
    pthread_mutex_unlock(&input_mutex);
}

void
lock_output_mutex ()
{
    pthread_mutex_lock(&output_mutex);
}

void
unlock_output_mutex ()
{
    pthread_mutex_unlock(&output_mutex);
}

void notify_input_ready ()
{
  pthread_cond_signal(&input_cond);
}

void wait_for_input ()
{
  pthread_cond_wait(&input_cond, &input_mutex);
}

void notify_output_ready ()
{
  pthread_cond_signal(&output_cond);
}

void wait_for_output ()
{
  pthread_cond_wait(&output_cond, &output_mutex);
}

void
put_alien_object_to_stream (Lisp_Object object, FILE *stream)
{
  Fprint(object, Qt);
}


void
convert_lisp_object_to_alien_data (Lisp_Object obj,
				   alien_data_struct *data)
{
  memset(data, 0, sizeof(alien_data_struct));
  data->type = XTYPE (obj);
  switch (data->type)
    {
    case_Lisp_Int:
      data->int_data = XFIXNUM (obj);
      break;
    case Lisp_Float:
      data->float_data = XFLOAT_DATA (obj);
      break;
    case Lisp_String:
      data->char_data = SSDATA (obj);
      break;
    case Lisp_Symbol:
      data->char_data = SSDATA (SYMBOL_NAME (obj));
      break;
    default:
      data->char_data = "unsupported";
    }
}

intercomm_session_struct intercomm_message;

void
alien_send_message (ptrdiff_t argc, Lisp_Object *argv)
{
  if (intercomm_active)
    {
      return;
    }
  lock_output_mutex();
  lock_input_mutex ();
  intercomm_active = true;
  long current_message_id = ++message_counter;
  intercomm_message.message_id = current_message_id;
  intercomm_message.alien_input_length = argc;
  intercomm_message.alien_input_array = (alien_data_struct*) malloc(sizeof(alien_data_struct) * argc);
  intercomm_message.alien_output_length = 0;
  intercomm_message.alien_output_array = NULL;
  for (int argi = 0; argi < argc; argi++)
    {
      convert_lisp_object_to_alien_data (argv[argi], &(intercomm_message.alien_input_array[argi]));
    }
  notify_input_ready();
  unlock_input_mutex();
  printf ("waiting for output, message_id=%ld\n", current_message_id);
  wait_for_output();
  if (current_message_id != intercomm_message.message_id)
    {
      printf("alien connection is broken, messages are incompatible\n");
      emacs_abort();
    }
  if (intercomm_message.alien_output_array != NULL)
    {
      free(intercomm_message.alien_output_array);
    }
  free(intercomm_message.alien_input_array);
  intercomm_active = false;
  unlock_output_mutex();
}
