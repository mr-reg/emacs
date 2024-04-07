#ifndef ALIEN_INTERCOMM
# define ALIEN_INTERCOMM
# include "config.h"
# include "lisp.h"

#define ALIEN_INTERCOMM_ENABLED 1

void alien_print_backtrace (void);
void init_alien_intercomm (void);

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
