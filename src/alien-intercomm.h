#ifndef ALIEN_INTERCOMM
# define ALIEN_INTERCOMM
# include "config.h"
# include "lisp.h"

void init_alien_intercomm ();
void alien_send_message (char* func, ptrdiff_t argc, Lisp_Object *argv);

void alien_send_message0(char* func);
void alien_send_message1(char* func, Lisp_Object arg0);
void alien_send_message2(char* func, Lisp_Object arg0, Lisp_Object arg1);
void alien_send_message3(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2);
void alien_send_message4(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3);
void alien_send_message5(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4);
void alien_send_message6(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4, Lisp_Object arg5);
void alien_send_message7(char* func, Lisp_Object arg0, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3, Lisp_Object arg4, Lisp_Object arg5, Lisp_Object arg6);
#endif
