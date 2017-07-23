/* 
 * File: vss.h
 * Author: Alex Brodsky
 * Purpose: Defines the virtual scheduler system (vss) structure and function 
 *          pointers.
 */

#ifndef VSS_H
#define VSS_H

#include "rcb.h"

typedef void (*submit_func)( rcb * );      /* function prototype for submit */
typedef rcb * (*get_next_func)( void );    /* function prototype for get_next */

typedef struct _vss vss;
struct _vss {
  char *        name;                      /* name of scheduler */
  submit_func   submit;                    /* func ptr to submit func */
  get_next_func get_next;                  /* func ptr tp get_next func */
};

#endif
