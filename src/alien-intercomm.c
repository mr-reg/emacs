#include "alien-intercomm.h"
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include "lisp.h"
#include <sys/resource.h>
#include <signal.h>
#include <threads.h>

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
mtx_t emacs_mutex;
/* mtx_t common_lisp_mutex; */
/* cnd_t emacs_cond, common_lisp_cond; */
/* int common_lisp_active = 0; */
long message_counter = 0;
sig_atomic_t marker = 0;
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


/* void */
/* lock_common_lisp_mutex () */
/* { */
/*   printf("TRY_LOCK common_lisp_mutex by thread %ld\n", thrd_current ()); */
/*   mtx_lock(&common_lisp_mutex); */
/*   printf("LOCKED   common_lisp_mutex %ld\n", thrd_current ()); */
/* } */

/* void */
/* unlock_common_lisp_mutex () */
/* { */
/*   printf("UNLOCK   common_lisp_mutex %ld\n", thrd_current ()); */
/*   mtx_unlock(&common_lisp_mutex); */
/* } */

void
lock_emacs_mutex ()
{
  printf("TRY_LOCK emacs_mutex %ld\n", thrd_current ());
  mtx_lock(&emacs_mutex);
  printf("LOCKED   emacs_mutex %ld\n", thrd_current ());
}

void
unlock_emacs_mutex ()
{
  printf("UNLOCK   emacs_mutex %ld\n", thrd_current ());
  mtx_unlock(&emacs_mutex);
}

/* void notify_emacs_cond () */
/* { */
/*   printf("NOTIFY   emacs_mutex %ld\n", thrd_current ()); */
/*   cnd_signal(&emacs_cond); */
/* } */

/* void wait_for_emacs_cond () */
/* { */
/*   printf("WAITSTRT emacs_mutex %ld\n", thrd_current ()); */
/*   cnd_wait(&emacs_cond, &emacs_mutex); */
/*   printf("WAITEND  emacs_mutex %ld\n", thrd_current ()); */
/* } */

/* void notify_common_lisp_cond () */
/* { */
/*   printf("NOTIFY   common_lisp_mutex %ld\n", thrd_current ()); */
/*   cnd_signal(&common_lisp_cond); */
/* } */

/* void wait_for_common_lisp_cond () */
/* { */
/*   printf("WAITSTRT common_lisp_mutex %ld\n", thrd_current ()); */
/*   cnd_wait(&common_lisp_cond, &common_lisp_mutex); */
/*   printf("WAITEND  common_lisp_mutex %ld\n", thrd_current ()); */
/* } */

void
put_alien_object_to_stream (Lisp_Object object, FILE *stream)
{
  Fprint(object, Qt);
}

void
init_alien_intercomm ()
{
  signal(SIGXCPU, SIG_IGN);
  if (! (sizeof (EMACS_INT) == sizeof (long)))
    {
      printf("EMACS_INT size is unexpected %ld\n", thrd_current ());
      emacs_abort();
    }
  /* if (common_lisp_active == 0) */
  /*   { */
  /*     wait_for_emacs_cond (); */
  /*     if (common_lisp_active == 0) */
  /* 	{ */
  /* 	  printf("common_lisp_active status error %ld\n", thrd_current ()); */
  /* 	  emacs_abort(); */
  /* 	} */
  /*   } */
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
  if (marker > 0)
    {
      printf("alien_send_message while inside intercomm %ld\n", thrd_current ());
      return;
    }
  lock_emacs_mutex();
  marker = 1;
  long current_message_id = ++message_counter;
  printf("emacs message id %ld\n", current_message_id);
  memset(&intercomm_message, 0, sizeof(intercomm_session_struct));
  if (argc > 0 && SYMBOLP(argv[0]) )
    {
      printf ("symbol %s\n", SSDATA (SYMBOL_NAME (argv[0])));
    }
  intercomm_message.message_id = current_message_id;
  /* intercomm_message.alien_input_length = argc; */
  /* intercomm_message.alien_input_array = (alien_data_struct*) malloc(sizeof(alien_data_struct) * argc); */
  /* intercomm_message.alien_output_length = 0; */
  /* intercomm_message.alien_output_array = NULL; */
  /* for (int argi = 0; argi < argc; argi++) */
  /*   { */
  /*     convert_lisp_object_to_alien_data (argv[argi], &(intercomm_message.alien_input_array[argi])); */
  /*   } */
  unlock_emacs_mutex();
  printf ("waiting for output, message_id=%ld\n", current_message_id);
  while (marker == 1)
    {
      usleep(1);
    }
  lock_emacs_mutex();
  printf ("output received, message_id=%ld\n", intercomm_message.message_id);
  if (current_message_id != intercomm_message.message_id)
    {
      printf("alien connection is broken, messages are incompatible %ld\n", thrd_current ());
      emacs_abort();
    }
  marker = 0;
  unlock_emacs_mutex();

  /* if (intercomm_message.alien_output_array != NULL) */
  /*   { */
  /*     free(intercomm_message.alien_output_array); */
  /*   } */
  /* free(intercomm_message.alien_input_array); */
}
