#ifndef ALIEN_INTERCOMM
# define ALIEN_INTERCOMM
# include "config.h"
# include "lisp.h"

#define ALIEN_INTERCOMM_ENABLED 1
#define ALIENP(v) (XTYPE (v) == Lisp_Symbol && (XSYMBOL(v)->u.s.redirect == SYMBOL_FORWARDED && ((struct Lisp_Objfwd *)XSYMBOL(v)->u.s.val.fwd.fwdptr)->type == Lisp_Fwd_Alien))

void alien_print_backtrace (void);
void init_alien_intercomm (void);
/* void visit_alien_roots (struct gc_root_visitor visitor); */

Lisp_Object alien_rpc (char* func, ptrdiff_t argc, Lisp_Object *argv);
Lisp_Object alien_rpc0(const char* func);
Lisp_Object alien_rpc1(const char* func, Lisp_Object arg0);
Lisp_Object alien_rpc2(const char* func, Lisp_Object arg0, Lisp_Object arg1);
Lisp_Object add_alien_forward_if_required(Lisp_Object var);
void fprint_lisp_object(Lisp_Object obj, FILE *stream);
/* Lisp_Object simple_eval (char* function, ptrdiff_t argc, Lisp_Object *argv); */
/* Lisp_Object simple_eval1 (const char* function, Lisp_Object arg0); */
/* Lisp_Object simple_eval2 (const char* function, Lisp_Object arg0, Lisp_Object arg1); */
void debug_lisp_object (const char* message, Lisp_Object obj);

EXFUN (Fcommon_lisp, 1);
EXFUN (Fcommon_lisp_init, 0);

#endif
