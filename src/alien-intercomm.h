#ifndef ALIEN_INTERCOMM
# define ALIEN_INTERCOMM
# include "config.h"
# include "lisp.h"

#define ALIEN_INTERCOMM_ENABLED 1

void alien_print_backtrace (void);
void init_alien_intercomm (void);
void alien_send_message (char* func, ptrdiff_t argc, Lisp_Object *argv);

void alien_send_message0(const char* func);
void alien_send_message1(const char* func, Lisp_Object arg0);
void alien_send_message2(const char* func, Lisp_Object arg0, Lisp_Object arg1);
void alien_send_message3(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2);
void alien_send_message4(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3);
void alien_send_message5(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4);
void alien_send_message6(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4, Lisp_Object arg5);
void alien_send_message7(const char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4, Lisp_Object arg5, Lisp_Object arg6);
void alien_send_message1n(const char* func, Lisp_Object arg0, ptrdiff_t argc, Lisp_Object *argv);
void alien_send_message2n(const char* func, Lisp_Object arg0, Lisp_Object arg1, ptrdiff_t argc, Lisp_Object *argv);

Lisp_Object alien_rpc (char* func, ptrdiff_t argc, Lisp_Object *argv);
Lisp_Object alien_rpc0(const char* func);
Lisp_Object alien_rpc1(const char* func, Lisp_Object arg0);
Lisp_Object alien_rpc2(const char* func, Lisp_Object arg0, Lisp_Object arg1);

void fprint_lisp_object(Lisp_Object obj, FILE *stream, int toplevel);
/* Lisp_Object simple_eval (char* function, ptrdiff_t argc, Lisp_Object *argv); */
/* Lisp_Object simple_eval1 (const char* function, Lisp_Object arg0); */
/* Lisp_Object simple_eval2 (const char* function, Lisp_Object arg0, Lisp_Object arg1); */
void debug_lisp_object (const char* message, Lisp_Object obj);

EXFUN (Fcommon_lisp, 1);
EXFUN (Fcommon_lisp_init, 0);

#endif
