#include "alien-intercomm.h"
#include "alien-injection.h"
#include "buffer.h"
/* #include <arpa/inet.h> // inet_addr() */
/* #include <netdb.h> */
/* #include <sys/socket.h> */
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
#include <zmq.h>
#include <sys/resource.h>


#define ALIEN_BACKTRACE_LIMIT 500
#define BACKTRACE_STR_SIZE 100000
#define ulong unsigned long
#define MESSAGE_TYPE_STOP_SERVER 100
#define MESSAGE_TYPE_NOTIFY_S_EXPR 1
#define MESSAGE_TYPE_SIGNAL 2
#define MESSAGE_TYPE_RPC 3

/* #define RPC_DEBUG */
/* #define ALIEN_VAR_DEBUG */
void add_alien_forward (Lisp_Object sym, Lisp_Object alien_symbol)
{
  struct Lisp_Objfwd const o_fwd = {Lisp_Fwd_Alien, &alien_symbol};
  /* XSYMBOL (sym)->u.s.declared_special = true; */
  XSYMBOL (sym)->u.s.redirect = SYMBOL_FORWARDED;
  SET_SYMBOL_FWD (XSYMBOL (sym), &o_fwd);
}

#define ALIEN_VAR_CACHE_SIZE 1000
unsigned char alien_var_cache[ALIEN_VAR_CACHE_SIZE] = { 0 };

#define MAX_STACK_SIZE 100000
Lisp_Object stack[MAX_STACK_SIZE] = { 0 };
unsigned long stack_position = 0;
void stack_push (Lisp_Object something)
{
  if (stack_position >= MAX_STACK_SIZE)
    {
      printf("stack exhausted\n");
      emacs_abort();
    }
  stack[stack_position] = something;
  stack_position++;
}
int stack_empty(void)
{
  if (stack_position == 0)
    {
      return 1;
    }
  else
    {
      return 0;
    }
}
Lisp_Object stack_pop (void)
{
  if (stack_position == 0)
    {
      return NULL;
    }
  else
    {
      stack_position--;
      return stack[stack_position];
    }
}
void stack_clear(void)
{
  stack_position = 0;
}

Lisp_Object
add_alien_forward_if_required (Lisp_Object var)
{
  if (XTYPE (var) != Lisp_Symbol)
    {
      return var;
    }
  /* struct Lisp_Symbol *sym = XSYMBOL (var); */
  if (ALIENP(var))
    {
#ifdef ALIEN_VAR_DEBUG
      debug_lisp_object ("already alien", var);
#endif
      return var;
    }
  unsigned id = XSYMBOL (var)->u.s.interned;
  if (id >= ALIEN_VAR_CACHE_SIZE)
    {
      printf("id too big: sym %s id %d\n", SSDATA (SYMBOL_NAME (var)), id);
      emacs_abort();
    }
  Lisp_Object boundp = Qnil;
  if (alien_var_cache[id] == 0)
    {
      boundp = Falien_boundp (make_string (SSDATA (SYMBOL_NAME (var)), SBYTES (SYMBOL_NAME (var))));
#ifdef ALIEN_VAR_DEBUG
      printf("storing cache: sym %s id %ld\n", SSDATA (SYMBOL_NAME (var)), SBYTES (SYMBOL_NAME (var)), id);
#endif
      if (NILP(boundp))
	{
	  alien_var_cache[id] = 2;
	}
      else
	{
	  alien_var_cache[id] = 1; //bound
	}
    }
  else if (alien_var_cache[id] == 1)
    {
      boundp = Qt;
    }
  else if (alien_var_cache[id] == 2)
    {
      boundp = Qnil;
    }
  else
    {
      printf("unknown value\n");
      emacs_abort();
    }
  if (NILP(boundp)) {
#ifdef ALIEN_VAR_DEBUG
    debug_lisp_object ("not alien", var);
#endif
    return var;
  } else
  {
#ifdef ALIEN_VAR_DEBUG
    debug_lisp_object ("now alien", var);
#endif
    add_alien_forward (var, var);
    return var;
  }
}


void *zmq_context;
void* zmq_client;

mtx_t intercomm_mutex;
char *backtrace_str[BACKTRACE_STR_SIZE];
long message_id = 0;
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

static void intercomm_die (const char* message)
{
  printf("DIE %s\n", message);
  mtx_unlock(&intercomm_mutex);
  emacs_abort();
  /* exit(1); */
}

static void zmq_check(ssize_t status)
{
  if (status != 0)
  {
    char buffer[200];
    sprintf(buffer, "socket operation error, status=%ld", status);
    /* intercomm_die(buffer); */
  }
}

Lisp_Object find_in_stack (Lisp_Object obj)
/* return Qnil if element not found in stack. Stack is not recursive */
{
  for (long pos = 0; pos < stack_position; pos++) {
    if (XLI(stack[pos]) == XLI(obj))
	{
	  return obj;
	}
  }
  return Qnil;
}

// IDASCRNP
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
      char *data = SSDATA (SYMBOL_NAME (obj));
      type = 'S';
      /* printf("symbol %s\n", data); */
      fwrite (&type, 1, 1, stream);
      fwrite (&len, sizeof (long), 1, stream);
      fwrite (data, 1, len, stream);
    }
    break;
    case Lisp_Cons:
    {
      type = 'C';
      fwrite (&type, 1, 1, stream);
      unsigned long cons_address = (unsigned long) obj;
      fwrite (&cons_address, sizeof (unsigned long), 1, stream);

      Lisp_Object check;
      check = find_in_stack (XCAR(obj));
      if (NILP(check))
	{
	  stack_push(XCAR(obj));
	  fwrite_lisp_binary_object (XCAR (obj), stream);
	}
      else
	{
	  type = 'R';
	  fwrite (&type, 1, 1, stream);
	  unsigned long xcar_address = (unsigned long) XCAR (obj);
	  fwrite (&xcar_address, sizeof (unsigned long), 1, stream);
	} 

      check = find_in_stack (XCDR(obj));
      if (NILP(check))
	{
	  stack_push(XCDR(obj));
	  fwrite_lisp_binary_object (XCDR (obj), stream);
	}
      else
	{
	  type = 'R';
	  fwrite (&type, 1, 1, stream);
	  unsigned long xcdr_address = (unsigned long) XCDR (obj);
	  fwrite (&xcdr_address, sizeof (unsigned long), 1, stream);
	} 
    }
    break;
    case Lisp_Vectorlike:
    {
      long vector_type = PSEUDOVECTOR_TYPE (XVECTOR (obj));
      switch (vector_type)
	{
	case PVEC_NORMAL_VECTOR:
	  {
	    /* printf("writing normal vector\n"); */
	    type = 'N';
	    fwrite(&type, 1, 1, stream);
	    long len = ASIZE(obj);
	    fwrite(&len, sizeof(long), 1, stream);
	    for (ptrdiff_t idx = 0; idx < len; idx ++)
	      {
		/* printf("writing element %ld from %ld\n", idx, len); */
		/* if (idx == 15) */
		/*   { */
		/*     debug_lisp_object ("=", AREF (obj, idx)); */
		/*   } */
		fwrite_lisp_binary_object(AREF(obj, idx), stream);
	      }
	  }
	  break;
	case PVEC_SUBR:
	  {
	    type = 'P';
	    fwrite (&type, 1, 1, stream);
	    fwrite (obj, 8, 1, stream);
	  }
	  break;
	default:
	  printf("write_binary: unsupported vector type %ld\n", vector_type);
	  emacs_abort();
	  break;
	}
    }
    break;
    default:
      fwrite_lisp_binary_object (build_string("write: unsupported type"), stream);
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
	result = intern (data);
	free (data);
      }
      break;
    case 'C':
      {
	long addr = 0;
	fread (&addr, sizeof(long), 1, stream);
	Lisp_Object cons = Fcons(Qnil, Qnil);
	stack_push(make_fixnum(addr));
	/* printf("addr %ld\n", addr); */
	XSETCAR (cons, fread_lisp_binary_object(stream));
	XSETCDR (cons, fread_lisp_binary_object(stream));
	result = cons;
      }
      break;
    case 'R':
      {
	long addr = 0;
	fread (&addr, sizeof (long), 1, stream);
	Lisp_Object laddr = make_fixnum(addr);

	result = NULL;
	for (long pos = 0; pos < stack_position; pos++) {
	  if (CONSP(stack[pos]) && EQ(XCAR(stack[pos]), laddr))
	    {
	      result = XCDR (stack[pos]);
	      break;
	    }
	}
	return Qnil;
	if (result == NULL)
	  {
	    printf("addr not found in stack %ld\n", addr);
	    emacs_abort();
	  }
      }
    /* case 'L': */
    /*   { */
    /* 	Lisp_Object cdr = fread_lisp_binary_object(stream); */
    /* 	result = Fcons(Qalien_var, cdr); */
    /*   } */
      break;
    case 'V':
      {
	long alien_idx = 0;
	fread (&alien_idx, sizeof (long), 1, stream);
	result = Fcons(Qalien_value, make_fixnum(alien_idx));
      }
      break;
    case 'N':
      {
	long vector_length = 0;
	fread (&vector_length, sizeof (long), 1, stream);
	result = make_vector (vector_length, Qnil);
	for (long idx = 0; idx < vector_length; idx++)
	  {
	    ASET(result, idx, fread_lisp_binary_object(stream));
	  }
      }
      break;
    case 'P':
      {
	fread(&result, 8, 1, stream);
      }
      break;
    default:
      {
	char buffer[100];
	sprintf (buffer, "read: unsupported type %d", c);
	result = build_string (buffer);
      }
    }
  return result;
}

void fprint_lisp_object(Lisp_Object obj, FILE *stream)
{
  /* add_alien_forward_if_required(obj); */
  int type = XTYPE (obj);
  if (ALIENP(obj))
    {
      fprintf (stream, "[alien]");
    }
  fprintf(stream, "[type:%d] ", type);
  switch (type)
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
	fprintf (stream, "'");

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
	Lisp_Object check = find_in_stack (XCAR(obj));
	if (NILP(check))
	  {
	    stack_push(XCAR(obj));
	    fprint_lisp_object (XCAR (obj), stream);
	  }
	else
	  {
	    fprint_lisp_object (build_string("recursion"), stream);
	  } 
	fprintf (stream, " ");

	check = find_in_stack (XCDR(obj));
	if (NILP(check))
	  {
	    stack_push(XCDR(obj));
	    fprint_lisp_object (XCDR (obj), stream);
	  }
	else
	  {
	    fprint_lisp_object (build_string("recursion"), stream);
	  } 

	fprintf (stream, ")");
      }
      break;
    case Lisp_Vectorlike:
      {
	long vector_type = PSEUDOVECTOR_TYPE (XVECTOR (obj));
	printf("vector %ld\n", vector_type);
      }
      break;
    default:
      fprintf(stream, "\"unsupported\"");
    }
}

void debug_lisp_object (const char* message, Lisp_Object obj)
{
  printf("%s ", message);
  stack_clear();
  fprint_lisp_object(obj, stdout);
  printf("\n");
  fflush(stdout);
}


Lisp_Object alien_rpc (char* func, ptrdiff_t argc, Lisp_Object *argv)
{
  /* debug_lisp_object("debug", Agcs_done); */
  /* if (strcmp("cl-emacs/elisp:alien-set-internal", func) == 0) */
  /*   { */
  /*   alien_print_backtrace(); */
  /*   } */
#ifdef RPC_DEBUG
  printf("RPC_DEBUG %s\n", func);
#endif
  int lock_status = mtx_trylock(&intercomm_mutex);
  if (lock_status != 0)
  {
    printf("taking rpc lock failed\n");
    alien_print_backtrace();
    emacs_abort();
  }
  stack_clear();
  int retry = 1;
  int nretry = 3;
  zmq_msg_t in_msg;
  while (retry) {
    retry = 0;
    message_id ++;
#ifdef RPC_DEBUG
    printf ("RPC_DEBUG[%ld] locked %s\n", message_id, func);
#endif

    char *sbuffer;
    size_t sbuffer_len;
    FILE *sstream = open_memstream(&sbuffer, &sbuffer_len);

    Lisp_Object context = Qnil;
    context = Fcons(intern(":message-id"), Fcons(make_fixnum(message_id), context));
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
    fwrite_lisp_binary_object(make_int(message_id), sstream);
    fwrite_lisp_binary_object(make_int(MESSAGE_TYPE_RPC), sstream);
    fwrite_lisp_binary_object(make_int(3), sstream); // nargs
    fwrite_lisp_binary_object(build_string(func), sstream);
    fwrite_lisp_binary_object(argl, sstream);
    fwrite_lisp_binary_object(context, sstream);
    fclose(sstream);

    zmq_msg_t out_msg;
    zmq_check(zmq_msg_init_size(&out_msg, sbuffer_len));
    memcpy(zmq_msg_data(&out_msg), sbuffer, sbuffer_len);
    free(sbuffer);
#ifdef RPC_DEBUG
    printf("RPC_DEBUG sending message func:%s (message length %ld)\n", func, sbuffer_len);
#endif
    zmq_check(zmq_msg_send(&out_msg, zmq_client, 0));
    zmq_check(zmq_msg_close(&out_msg));
#ifdef RPC_DEBUG
    printf("RPC_DEBUG receiving message\n");
#endif
    zmq_check(zmq_msg_init(&in_msg));
    zmq_check(zmq_msg_recv(&in_msg, zmq_client, 0));
    long msg_size = zmq_msg_size(&in_msg);
#ifdef RPC_DEBUG
    printf("RPC_DEBUG message size %ld\n", msg_size);
#endif
    if (msg_size == 0 && nretry > 0) {
      printf ("retrying %d\n", nretry);
      zmq_check(zmq_msg_close(&in_msg));
      retry = 1;
      nretry --;
    }
  }
  mtx_unlock(&intercomm_mutex);
#ifdef RPC_DEBUG
  char* data = zmq_msg_data(&in_msg);
  for (long addr = 0; addr < zmq_msg_size(&in_msg); addr++)
  {
    printf("%d ", data[addr]);
  }
  printf("\n");
#endif
  stack_clear();
  FILE *stream = fmemopen(zmq_msg_data(&in_msg), zmq_msg_size(&in_msg), "r");
  Lisp_Object lmessage_type = fread_lisp_binary_object(stream);
  Lisp_Object result = fread_lisp_binary_object(stream);
#ifdef RPC_DEBUG
  debug_lisp_object("RPC_DEBUG message_type: ", lmessage_type);
  debug_lisp_object("RPC_DEBUG response: ", result);
#endif
  /* printf("before read\n"); */
  /* printf("after read\n"); */
  long message_type = XFIXNUM (lmessage_type);

  if (message_type == MESSAGE_TYPE_SIGNAL)
    {
      Fsignal(XCAR(result), XCDR(result));
    }
  else if (message_type == MESSAGE_TYPE_RPC)
    {
      // do nothing
    }
  else
    {
      printf("unknown message type %ld", message_type);
      emacs_abort();
      alien_print_backtrace();
    }

  zmq_check(zmq_msg_close(&in_msg));
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


/* Lisp_Object make_alien (char* name, ptrdiff_t size) */
/* { */
/*   Lisp_Object symbol = Fmake_remote_var(make_string(name, size)); */
/*   return Fcons(Qalien_var, symbol); */
/* } */

/* Lisp_Object build_alien (char* name) */
/* { */
/*   return make_alien (name, strlen(name)); */
/* } */

/* void */
/* visit_alien_roots (struct gc_root_visitor visitor) */
/* { */
/*   visitor.visit(&Agcs_done, GC_ROOT_C_SYMBOL, visitor.data); */
/* } */

void
init_alien_intercomm (void)
{
  printf("sizeof(union vectorlike_header)=%ld\n", sizeof(union vectorlike_header));
  printf("sizeof(Lisp_Object)=%ld\n", sizeof(Lisp_Object));
  mtx_init(&intercomm_mutex, mtx_plain);
  zmq_context = zmq_ctx_new();
  printf("zmq initialized\n");
  DEFSYM (Qalien_value, "alien-value");
  char intercomm_addr[] = "tcp://127.0.0.1:7447";
  zmq_client = zmq_socket(zmq_context, ZMQ_REQ);
  if (!zmq_client)
  {
    intercomm_die("can't create socket");
  }
  zmq_check(zmq_connect(zmq_client, intercomm_addr));
  Finit_globals();
  defsubr (&Scommon_lisp_apply);
  defsubr (&Scommon_lisp_init);

  ///// init done

  /* Lisp_Object map = Fmake_hash_table2(0, NULL); */
  /* add_alien_forward_if_required(map); */
  /* debug_lisp_object("map", map); */
  /* Fputhash2(make_fixnum(30), make_fixnum(240), map); */
  /* Lisp_Object test = Fgethash2(make_fixnum(30), map, make_fixnum(10)); */
  /* debug_lisp_object("result", test); */
  
  /* const rlim_t kStackSize = 100 * 1000 * 1024;   // min stack size = 16 MB */
  /* struct rlimit rl; */
  /* int result; */
  /* result = setrlimit(RLIMIT_STACK, &rl); */
  /* printf("stack result %d\n", result); */
  
  /* Lisp_Object n = intern("nil"); */
  /* printf("test %d\n", NILP(n)); */


  /* ////// recursion test */
  /* Lisp_Object */
  /*   num1 = make_fixnum(1), */
  /*   num2 = make_fixnum(2); */
  /* // (1 (2 (#))) */
  /* Lisp_Object */
  /*   cons2 = Fcons(num2, num1), */
  /*   cons1 = Fcons(num1, cons2); */
  /* XSETCDR (cons2, cons1); */
  /* debug_lisp_object("cons:", cons1); */
  /* Lisp_Object result = Falien_set_internal(Atest_alien_var, cons1, Qnil, Qnil); */
  /* debug_lisp_object("result:", result); */
  
  /* exit(0); */

  /* Lisp_Object test = intern("test1"); */
  /* Fset(test, make_fixnum(34)); */
  /* /\* Fincrement(test); *\/ */
  /* debug_lisp_object("result ", Fsymbol_value(test)); */
  /* exit(0); */
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
