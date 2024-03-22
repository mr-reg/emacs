#include "alien-intercomm.h"
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
/* #include <threads.h> */

#define ALIEN_BACKTRACE_LIMIT 500
#define BACKTRACE_STR_SIZE 100000
#define ulong unsigned long
#define MESSAGE_TYPE_STOP_SERVER 0
#define MESSAGE_TYPE_NOTIFY_S_EXPR 1
#define MESSAGE_TYPE_ERROR 2

char *backtrace_str[BACKTRACE_STR_SIZE];

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
put_alien_object_to_stream (Lisp_Object object, FILE *stream)
{
  Fprint(object, Qt);
}


char intercomm_host[] = "127.0.0.1";
int intercomm_port = 7447;
int open_intercomm_connection ()
{
  struct sockaddr_in servaddr, cli;
  // socket create and verification
  int intercomm_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (intercomm_socket == -1) {
    printf("intercomm socket creation failed.\n");
    emacs_abort();
  } 

  struct timeval tv;
  tv.tv_sec = 60; // socket timeout
  tv.tv_usec = 0;
  setsockopt(intercomm_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

  bzero(&servaddr, sizeof(servaddr));

  // assign IP, PORT
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(intercomm_host);
  servaddr.sin_port = htons(intercomm_port);

  // connect the client socket to server socket
  if (connect(intercomm_socket, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
    printf("intercomm connection failed.\n");
    emacs_abort();
  } 
  return intercomm_socket;
}

void
init_alien_intercomm ()
{
}

void fprint_lisp_object(Lisp_Object obj, FILE *stream)
{
  switch (XTYPE (obj))
    {
    case_Lisp_Int:
    {
	fprintf (stream, " %ld", XFIXNUM (obj));
    }
    break;
    case Lisp_Float:
    {
	fprintf (stream, " %lf", XFLOAT_DATA (obj));
    }
    break;
    case Lisp_String:
    {
	fprintf (stream, " \"%s\"", SSDATA (obj));
    }
    break;
    case Lisp_Symbol:
    {
	fprintf (stream, " '%s", SSDATA (SYMBOL_NAME (obj)));
    }
    break;
    case Lisp_Cons:
    {
	fprintf (stream, " (cons");
	fprint_lisp_object (XCAR (obj), stream);
	fprint_lisp_object (XCDR (obj), stream);
	fprintf (stream, ")");
    }
    break;
    case Lisp_Vectorlike:
    {
	enum pvec_type vector_type
	  = PSEUDOVECTOR_TYPE (XVECTOR (obj));
	fprintf (stream, " \"unsupported vector type %d\"",
		 vector_type);
    }
    break;
    default:
      fprintf(stream, " 'unsupported");
    }
}

void check_socket_operation(ssize_t status)
{
  if (status == -1)
  {
    printf("socket operation error\n");
    emacs_abort();
  }
}

void
alien_send_message (char* func, ptrdiff_t argc, Lisp_Object *argv)
{
  int intercomm_socket = open_intercomm_connection();

  char *sbuffer;
  size_t sbuffer_len;
  FILE *sstream = open_memstream(&sbuffer, &sbuffer_len);
  fprintf(sstream, "(%s", func);
  for (int argi = 0; argi < argc; argi++)
    {
      fprint_lisp_object(argv[argi], sstream);
    }
  fprintf(sstream, ")");
  /* fprintf(sstream, "#|%s|#", get_alien_backtrace()); */
  fclose(sstream);
  ulong message_length = sbuffer_len;
  /* printf("sending message %s (length %ld)\n", message, message_length); */
  check_socket_operation(send(intercomm_socket, &message_length, sizeof(ulong), 0));
  ulong message_type = MESSAGE_TYPE_NOTIFY_S_EXPR;
  check_socket_operation(send(intercomm_socket, &message_type, sizeof(ulong), 0));
  check_socket_operation(send(intercomm_socket, sbuffer, message_length, 0));
  free(sbuffer);
  check_socket_operation(recv(intercomm_socket, &message_length, sizeof(ulong), 0));
  check_socket_operation(recv(intercomm_socket, &message_type, sizeof(ulong), 0));
  char *response = malloc(message_length + 1);
  check_socket_operation(recv(intercomm_socket, response, message_length, 0));
  response[message_length] = 0;
  /* printf("response %s\n", response); */
  free (response);
  close(intercomm_socket);
}

void alien_send_message0(char* func)
{
  Lisp_Object alien_data[] = {  };
  alien_send_message(func, 0, alien_data);
}

void alien_send_message1(char* func, Lisp_Object arg0)
{
  Lisp_Object alien_data[] = { arg0 };
  alien_send_message(func, 1, alien_data);
}

void alien_send_message2(char* func, Lisp_Object arg0, Lisp_Object arg1)
{
  Lisp_Object alien_data[] = { arg0, arg1 };
  alien_send_message(func, 2, alien_data);
}

void alien_send_message3(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2 };
  alien_send_message(func, 3, alien_data);
}

void alien_send_message4(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2, arg3 };
  alien_send_message(func, 4, alien_data);
}

void alien_send_message5(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2, arg3, arg4 };
  alien_send_message(func, 5, alien_data);
}

void alien_send_message6(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4, Lisp_Object arg5)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2, arg3, arg4, arg5 };
  alien_send_message(func, 6, alien_data);
}

void alien_send_message7(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4, Lisp_Object arg5, Lisp_Object arg6)
{
  Lisp_Object alien_data[] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
  alien_send_message(func, 7, alien_data);
}
