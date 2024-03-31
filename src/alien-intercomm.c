#include "alien-intercomm.h"
#include "buffer.h"
#include <arpa/inet.h> // inet_addr()
#include <netdb.h>
#include <sys/socket.h>
#include <execinfo.h>
#include <stdio.h>
#include <strings.h> // bzero()
#include <unistd.h> // read(), write(), close()
#include <stdlib.h>
/* #include "lisp.h" */
/* #include <sys/resource.h> */
/* #include <signal.h> */
#include <threads.h>
#include "lisp.h"


#define ALIEN_BACKTRACE_LIMIT 500
#define BACKTRACE_STR_SIZE 100000
#define ulong unsigned long
#define MESSAGE_TYPE_STOP_SERVER 100
#define MESSAGE_TYPE_NOTIFY_S_EXPR 1
#define MESSAGE_TYPE_SIGNAL 2
#define MESSAGE_TYPE_RPC 3

mtx_t intercomm_mutex;
char *backtrace_str[BACKTRACE_STR_SIZE];

static char *
get_alien_backtrace (void)
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

void alien_print_backtrace (void)
{
  printf("%s\n", get_alien_backtrace());
}

static void intercomm_die (char* message)
{
  printf("DIE %s\n", message);
  mtx_unlock(&intercomm_mutex);
  exit(1);
}

char intercomm_host[] = "127.0.0.1";
int intercomm_port = 7447;
static int open_intercomm_connection (void)
{
  struct sockaddr_in servaddr;
  // socket create and verification
  int intercomm_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (intercomm_socket == -1) {
    intercomm_die((char*)"intercomm socket creation failed.");
  } 
  bzero(&servaddr, sizeof(servaddr));

  // assign IP, PORT
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(intercomm_host);
  servaddr.sin_port = htons(intercomm_port);

  int retries = 3, result = -1;
  while (retries > 0 && result != 0) {
  // connect the client socket to server socket
    result = connect(intercomm_socket, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (result != 0)
	{
	  printf ("intercomm connection error, retrying\n");
	  usleep (1000);
	}
    retries--;
  } 
  if (result != 0)
  {
    intercomm_die ((char*)"intercomm connection failed.");
  }
  return intercomm_socket;
}

void fwrite_lisp_binary_object(Lisp_Object obj, FILE *stream) {
  char type = 0;
  switch (XTYPE (obj))
    {
    case_Lisp_Int:
    {
      long value = XFIXNUM (obj);
      type = 'I';
      fwrite(&type, 1, 1, stream);
      fwrite(&value, sizeof(long), 1, stream);
    }
    break;
    case Lisp_Float:
    {
      double value = XFLOAT_DATA (obj);
      type = 'D';
      fwrite(&type, 1, 1, stream);
      fwrite(&value, sizeof(double), 1, stream);
    }
    break;
    case Lisp_String:
      {
	char* data = SSDATA (obj);
	long len = SBYTES (obj);
	type = 'A';
	fwrite(&type, 1, 1, stream);
	fwrite(&len, sizeof(long), 1, stream);
	/* printf("%ld %s\n", len, data); */
	fwrite(data, 1, len, stream);
    }
    break;
    case Lisp_Symbol:
    {
      long len = SBYTES (SYMBOL_NAME (obj));
      char* data = SSDATA (SYMBOL_NAME (obj));
      type = 'S';
      fwrite(&type, 1, 1, stream);
      fwrite(&len, sizeof(long), 1, stream);
      fwrite(data, 1, len, stream);
    }
    break;
    case Lisp_Cons:
    {
      type = 'C';
      fwrite(&type, 1, 1, stream);
      fwrite_lisp_binary_object (XCAR (obj), stream);
      fwrite_lisp_binary_object (XCDR (obj), stream);
    }
    break;
    case Lisp_Vectorlike:
    {
      char str[100];
      enum pvec_type vector_type = PSEUDOVECTOR_TYPE (XVECTOR (obj));
      sprintf(str, "unsupported vector type %d",
	      vector_type);
      fwrite_lisp_binary_object (build_string(str), stream);
    }
    break;
    default:
      fwrite_lisp_binary_object (build_string("unsupported type"), stream);
    }
}

Lisp_Object fread_lisp_binary_object(FILE *stream) {
  Lisp_Object result = Qnil;
  char c;
  fread(&c, 1, 1, stream);
  switch (c)
    {
    case 'I':
    {
      long value = 0;
      fread (&value, sizeof(long), 1, stream);
      result = make_fixnum(value);
    }
    break;
    case 'D':
    {
      double value = 0;
      fread(&value, sizeof(double), 1, stream);
      result = make_float(value);
    }
    break;
    case 'A':
      {
	long len = 0;
	fread (&len, sizeof(long), 1, stream);
	char* data = malloc(len + 1);
	fread (data, 1, len, stream);
	data[len] = 0;
	result = make_string(data, len);
	/* printf("type %ld\n", XTYPE(result)); */
	free (data);
    }
    break;
    case 'S':
    {
	long len = 0;
	fread (&len, sizeof(long), 1, stream);
	char* data = malloc(len + 1);
	fread (data, 1, len, stream);
	data[len] = 0;
	result = intern(data);
	free (data);
    }
    break;
    case 'C':
    {
      Lisp_Object car = fread_lisp_binary_object(stream);
      Lisp_Object cdr = fread_lisp_binary_object(stream);
      result = Fcons(car, cdr);
    }
    break;
    default:
      result = build_string("unsupported type");
    }
  return result;
}


void fprint_lisp_object(Lisp_Object obj, FILE *stream, int toplevel)
{
  switch (XTYPE (obj))
    {
    case_Lisp_Int:
    {
	fprintf (stream, "%ld", XFIXNUM (obj));
    }
    break;
    case Lisp_Float:
    {
	fprintf (stream, "%lf", XFLOAT_DATA (obj));
    }
    break;
    case Lisp_String:
      {
	fprintf (stream, "\"");
	char* data = SSDATA (obj);
	for (int idx = 0; idx < SCHARS (obj); idx++)
	  {
	    if (data[idx] == '"'
		|| data[idx] == '\\') {
	      fprintf (stream, "\\");
	    }
	    fprintf (stream, "%c", data[idx]);
	  }
	fprintf (stream, "\"");
    }
    break;
    case Lisp_Symbol:
    {
      char* symbol_name = SSDATA (SYMBOL_NAME (obj));
      if (strcmp(symbol_name, "`") == 0)
	  {
	    symbol_name = (char*)"backquote";
	  }
      if (strcmp(symbol_name, ",") == 0)
	  {
	    symbol_name = (char*)"comma";
	  }
      if (toplevel)
	{
	  fprintf (stream, "'");
	}

	for (int idx = 0; idx < strlen(symbol_name); idx++)
	  {
	    char c = symbol_name[idx];
	    if ((c >= 'A' && c <= 'Z') || c == '_') {
	      fprintf (stream, "_");
	    }
	    fprintf (stream, "%c", c);
	  }
    }
    break;
    case Lisp_Cons:
    {
	fprintf (stream, "(cons ");
	fprint_lisp_object (XCAR (obj), stream, 1);
	fprintf (stream, " ");
	fprint_lisp_object (XCDR (obj), stream, 1);
	fprintf (stream, ")");
    }
    break;
    case Lisp_Vectorlike:
    {
	enum pvec_type vector_type
	  = PSEUDOVECTOR_TYPE (XVECTOR (obj));
	fprintf (stream, "\"unsupported vector type %d\"",
		 vector_type);
    }
    break;
    default:
      fprintf(stream, "\"unsupported\"");
    }
}

void debug_lisp_object (const char* message, Lisp_Object obj)
{
  printf("%s ", message);
  fprint_lisp_object(obj, stdout, 1);
  printf("\n");
}

static void check_socket_operation(ssize_t status)
{
  if (status == -1)
  {
    char buffer[200];
    sprintf(buffer, "socket operation error, status=%ld", status);
    /* intercomm_die(buffer); */
  }
}

void alien_send_message (char* func, ptrdiff_t argc, Lisp_Object *argv)
{
  int failed = 1;
  while (failed) {
    int lock_status = mtx_trylock(&intercomm_mutex);
    if (lock_status != 0)
      {
	printf("taking message lock failed\n");
	alien_print_backtrace();
	emacs_abort();
      }
    char *sbuffer;
    size_t sbuffer_len;
    FILE *sstream = open_memstream(&sbuffer, &sbuffer_len);
    /* fwrite_lisp_binary_object(make_fixnum(argc + 1), stdout); */
    /* printf("AAAA"); */
    /* fwrite_lisp_binary_object(build_string(func), stdout); */
    /* printf("AAAA"); */
    fwrite_lisp_binary_object(make_fixnum(argc + 1), sstream);
    fwrite_lisp_binary_object(build_string(func), sstream);

    
    for (int argi = 0; argi < argc; argi++)
      {
	fwrite_lisp_binary_object(argv[argi], sstream);
      }
    /* fprintf(sstream, "#|%s|#", get_alien_backtrace()); */
    fclose (sstream);
    ulong message_length = sbuffer_len;
    /* printf ("sending message %s\n", sbuffer); */
    int intercomm_socket = open_intercomm_connection ();
    check_socket_operation (
			    send (intercomm_socket, &message_length, sizeof (ulong), 0));
    ulong message_type = MESSAGE_TYPE_NOTIFY_S_EXPR;
    /* printf ("sending message type\n"); */
    check_socket_operation (
			    send (intercomm_socket, &message_type, sizeof (ulong), 0));
    /* printf ("sending message body\n"); */
    check_socket_operation (
			    send (intercomm_socket, sbuffer, message_length, 0));
    free (sbuffer);
    /* printf ("readng message length\n"); */
    check_socket_operation (recv (intercomm_socket, &message_length, sizeof (ulong), 0));
    /* printf ("readng message type\n"); */
    check_socket_operation (recv (intercomm_socket, &message_type, sizeof (ulong), 0));
    char *response = malloc (message_length + 1);
    /* printf ("readng message body\n"); */
    check_socket_operation (recv (intercomm_socket, response, message_length, 0));
    response[message_length] = 0;
    /* printf("response %s\n", response); */
    free (response);
    close (intercomm_socket);
    failed = 0;
    mtx_unlock (&intercomm_mutex);
  }
}

void alien_send_message0(const char* func)
{
  Lisp_Object alien_data[] = {  };
  alien_send_message((char*)func, 0, alien_data);
}

void alien_send_message1(const char* func, Lisp_Object arg0)
{
  Lisp_Object alien_data[] = { arg0 };
  alien_send_message((char*)func, 1, alien_data);
}

void alien_send_message2(const char* func, Lisp_Object arg0, Lisp_Object arg1)
{
  Lisp_Object alien_data[] = { arg0, arg1 };
  alien_send_message((char*)func, 2, alien_data);
}

void alien_send_message3(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2 };
  alien_send_message((char*)func, 3, alien_data);
}

void alien_send_message4(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2, arg3 };
  alien_send_message((char*)func, 4, alien_data);
}

void alien_send_message5(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2, arg3, arg4 };
  alien_send_message((char*)func, 5, alien_data);
}

void alien_send_message6(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4, Lisp_Object arg5)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2, arg3, arg4, arg5 };
  alien_send_message((char*)func, 6, alien_data);
}

void alien_send_message7(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4, Lisp_Object arg5, Lisp_Object arg6)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
  alien_send_message((char*)func, 7, alien_data);
}

void alien_send_message1n(const char* func, Lisp_Object arg0, ptrdiff_t argc, Lisp_Object *argv)
{
  ptrdiff_t nargc = argc + 1;
  Lisp_Object *alien_data = malloc (sizeof(Lisp_Object) * nargc);
  alien_data[0] = arg0;
  memcpy(alien_data + 1, argv, argc * sizeof(Lisp_Object));
  alien_send_message((char*)func, nargc, alien_data);
  free(alien_data);
}

void alien_send_message2n(const char* func, Lisp_Object arg0, Lisp_Object arg1, ptrdiff_t argc, Lisp_Object *argv)
{
  ptrdiff_t nargc = argc + 2;
  Lisp_Object *alien_data = malloc (sizeof(Lisp_Object) * nargc);
  alien_data[0] = arg0;
  alien_data[1] = arg1;
  memcpy(alien_data + 2, argv, argc * sizeof(Lisp_Object));
  alien_send_message((char*)func, nargc, alien_data);
  free(alien_data);
}

Lisp_Object alien_rpc (char* func, ptrdiff_t argc, Lisp_Object *argv)
{

  char *sbuffer;
  size_t sbuffer_len;
  FILE *sstream = open_memstream(&sbuffer, &sbuffer_len);

  Lisp_Object context = Qnil;
  context = Fcons(intern(":invocation-directory"), Fcons(Vinvocation_directory, context));
  context = Fcons(intern(":home"), Fcons(Fgetenv_internal(build_string("HOME"), Qnil), context));
  if (current_buffer)
  {
    context = Fcons(intern(":buffer-default-directory"), Fcons(BVAR (current_buffer, directory), context));
  }
  Lisp_Object argl = Qnil;
  for (int argi = argc - 1; argi >= 0; argi--)
    {
      argl = Fcons(argv[argi], argl);
    }
  fwrite_lisp_binary_object(make_fixnum(3), sstream);
  fwrite_lisp_binary_object(build_string(func), sstream);
  fwrite_lisp_binary_object(argl, sstream);
  fwrite_lisp_binary_object(context, sstream);
  fclose(sstream);
  ulong message_length = sbuffer_len;
  int lock_status = mtx_trylock(&intercomm_mutex);
  if (lock_status != 0)
  {
    printf("taking rpc lock failed\n");
    alien_print_backtrace();
    emacs_abort();
  }
  int intercomm_socket = open_intercomm_connection();
  /* printf("sending message func:%s (message length %ld)\n", func, message_length); */
  check_socket_operation(send(intercomm_socket, &message_length, sizeof(ulong), 0));
  ulong message_type = MESSAGE_TYPE_RPC;
  check_socket_operation(send(intercomm_socket, &message_type, sizeof(ulong), 0));
  check_socket_operation(send(intercomm_socket, sbuffer, message_length, 0));
  free(sbuffer);
  check_socket_operation(recv(intercomm_socket, &message_length, sizeof(ulong), 0));
  check_socket_operation(recv(intercomm_socket, &message_type, sizeof(ulong), 0));
  char *response = malloc(message_length + 1);
  check_socket_operation(recv(intercomm_socket, response, message_length, 0));
  response[message_length] = 0;
  close(intercomm_socket);
  mtx_unlock(&intercomm_mutex);
  /* printf("rpc response %s\n", response); */
  FILE *stream = fmemopen(response, message_length, "r");
  Lisp_Object result = fread_lisp_binary_object(stream);
  free (response);
  /* printf("type2 %ld\n", XTYPE(result)); */
  /* debug_lisp_object("response: ", result); */
  /* printf("before read\n"); */
  /* printf("after read\n"); */

  if (message_type == MESSAGE_TYPE_NOTIFY_S_EXPR)
    {
      // do nothing
    }
  else if (message_type == MESSAGE_TYPE_SIGNAL)
    {
      Fsignal(XCAR(result), XCDR(result));
    }
  else
    {
      printf("unknown message type %ld", message_type);
      alien_print_backtrace();
    }
  /* if (strcmp(func, "elisp/expand-file-name") == 0) */
  /* { */
  /*   Lisp_Object orig = Fexpand_file_name(argv[0], argv[1]); */
  /*   if (NILP(Fstring_equal(orig, result))) { */
  /*     printf("debug compare rpc:"); */
  /*     fprint_lisp_object(result, stdout);  */
  /*     printf(" native:"); */
  /*     fprint_lisp_object(orig, stdout);  */
  /*     printf("\n"); */
  /*     emacs_abort(); */
  /*   } */
  /* } */

  /* printf("lisp_string:"); */
  /* fprint_lisp_object(lisp_string, stdout); */
  /* printf("\n"); */
  /* printf("read-from-string:"); */
  /* fprint_lisp_object(Fread_from_string(lisp_string, Qnil, Qnil), stdout); */
  /* printf("\n"); */
  return result;
}

Lisp_Object alien_rpc0(const char* func)
{
  Lisp_Object alien_data[] = { };
  return alien_rpc((char*)func, 0, alien_data);
}

Lisp_Object alien_rpc1(const char* func, Lisp_Object arg0)
{
  Lisp_Object alien_data[] = { arg0 };
  return alien_rpc((char*)func, 1, alien_data);
}
 
Lisp_Object alien_rpc2(const char* func, Lisp_Object arg0, Lisp_Object arg1)
{
  Lisp_Object alien_data[] = { arg0, arg1 };
  return alien_rpc((char*)func, 2, alien_data);
}


DEFUN ("common-lisp-apply", Fcommon_lisp_apply, Scommon_lisp_apply, 1, 2, 0,
       doc: /* Common lisp bridge */)
  (Lisp_Object function, Lisp_Object arglist)
{
  return alien_rpc2((char*)"apply", function, arglist);
}

DEFUN ("common-lisp-init", Fcommon_lisp_init, Scommon_lisp_init, 0, 0, 0,
       doc: /* Common lisp initialization */)
  (void)
{
  Lisp_Object code = alien_rpc0((char*)"cl-emacs/main:generate-elisp-block");
  return Feval(Fcar(Fread_from_string(code, Qnil, Qnil)), Qnil);
}

void
init_alien_intercomm (void)
{
  printf("sizeof(union vectorlike_header)=%ld\n", sizeof(union vectorlike_header));
  printf("sizeof(Lisp_Object)=%ld\n", sizeof(Lisp_Object));
  mtx_init(&intercomm_mutex, mtx_plain);
  defsubr (&Scommon_lisp_apply);
  defsubr (&Scommon_lisp_init);
  /* if (ALIEN_INTERCOMM_ENABLED)  */
  /* { */
  /*   Fcommon_lisp_init(); */
  /* } */

  /* char path[] = "subdirs.el"; */
  /* Lisp_Object debug = Fexpand_file_name(build_string(path), Qnil); */
  /* printf("debug:"); */
  /* fprint_lisp_object(debug, stdout); */
  /* printf("\n"); */
  /* exit(0); */
}

/* Lisp_Object simple_eval (char* function, ptrdiff_t argc, Lisp_Object *argv) */
/* { */
/*   /\* ptrdiff_t n_new_args = 1 + argc; *\/ */
/*   /\* Lisp_Object *new_args = malloc(n_new_args * sizeof(Lisp_Object)); *\/ */
/*   /\* memcpy(new_args + 1, argv, argc); *\/ */
/*   /\* new_args[0] = intern(function); *\/ */
/*   alien_print_backtrace(); */
/*   return alien_rpc(function, argc, argv); */
/* } */


/* Lisp_Object simple_eval1 (const char* function, Lisp_Object arg0) */
/* { */
/*   Lisp_Object alien_data[] = { arg0 }; */
/*   return simple_eval(function, 1, alien_data); */
/* } */

/* Lisp_Object simple_eval2 (const char* function, Lisp_Object arg0, Lisp_Object arg1) */
/* { */
/*   Lisp_Object alien_data[] = { arg0, arg1 }; */
/*   return simple_eval(function, 2, alien_data); */
/* } */
