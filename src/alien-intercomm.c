#include "alien-intercomm.h"
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "lisp.h"

# define ALIEN_BACKTRACE_LIMIT 500
# define BACKTRACE_STR_SIZE 100000
char *backtrace_str[BACKTRACE_STR_SIZE];
typedef struct
{
  enum Lisp_Type type;
  long int int_data;
  char *char_data;
  double float_data;
} alien_data_struct;
pthread_mutex_t alien_mutex;
bool intercomm_active = false;

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
  if (! (sizeof (EMACS_INT) == sizeof (long int)))
    {
      printf("EMACS_INT size is unexpected");
      emacs_abort();
    }
}

void
lock_alien_mutex ()
{
    pthread_mutex_lock(&alien_mutex);
}

void
unlock_alien_mutex ()
{
    pthread_mutex_unlock(&alien_mutex);
}

void
put_alien_object_to_stream (Lisp_Object object, FILE *stream)
{
  Fprint(object, Qt);
}

ptrdiff_t alien_data_length;
alien_data_struct *alien_data_array;

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

void
alien_send_message (ptrdiff_t argc, Lisp_Object *argv)
{
  if (intercomm_active)
    {
      return;
    }
  lock_alien_mutex ();
  alien_data_length = argc;
  alien_data_array = (alien_data_struct*) malloc(sizeof(alien_data_struct) * argc);
  intercomm_active = true;
  /* printf ("send_message started, argc=%ld\n", argc); */

  for (int argi = 0; argi < argc; argi++)
    {
      convert_lisp_object_to_alien_data (argv[argi], &alien_data_array[argi]);
    }

  free(alien_data_array);
  intercomm_active = false;
  unlock_alien_mutex();
}
