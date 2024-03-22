#ifndef ALIEN_INTERCOMM
# define ALIEN_INTERCOMM
# include "config.h"
# include "lisp.h"

void
init_alien_intercomm ();
void
alien_send_message (char* func, ptrdiff_t argc, Lisp_Object *argv);
#endif
