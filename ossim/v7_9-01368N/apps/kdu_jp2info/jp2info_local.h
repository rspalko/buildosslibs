/*****************************************************************************/
// File: jp2info_local.h [scope = APPS/JP2INFO]
// Version: Kakadu, V7.9
// Author: David Taubman
// Last Revised: 8 January, 2017
/*****************************************************************************/
// Copyright 2001, David Taubman, The University of New South Wales (UNSW)
// The copyright owner is Unisearch Ltd, Australia (commercial arm of UNSW)
// Neither this copyright statement, nor the licensing details below
// may be removed from this file or dissociated from its contents.
/*****************************************************************************/
// Licensee: Open Systems Integration; Inc
// License number: 01368
// The licensee has been granted a NON-COMMERCIAL license to the contents of
// this source file.  A brief summary of this license appears below.  This
// summary is not to be relied upon in preference to the full text of the
// license agreement, accepted at purchase of the license.
// 1. The Licensee has the right to install and use the Kakadu software and
//    to develop Applications for the Licensee's own use.
// 2. The Licensee has the right to Deploy Applications built using the
//    Kakadu software to Third Parties, so long as such Deployment does not
//    result in any direct or indirect financial return to the Licensee or
//    any other Third Party, which further supplies or otherwise uses such
//    Applications.
// 3. The Licensee has the right to distribute Reusable Code (including
//    source code and dynamically or statically linked libraries) to a Third
//    Party, provided the Third Party possesses a license to use the Kakadu
//    software, and provided such distribution does not result in any direct
//    or indirect financial return to the Licensee.
/******************************************************************************
Description:
   Local definitions used by "kdu_jp2info.cpp".
******************************************************************************/

#ifndef JP2INFO_LOCAL_H
#define JP2INFO_LOCAL_H

#include <stdio.h>
#include <assert.h>
#include "kdu_messaging.h"

// OK to have "using" statement in this header because it is intended to
// be private to the "kdu_jp2info" demo app.
using namespace kdu_supp; // Also includes the `kdu_core' namespace


/*****************************************************************************/
/*                            kd_indented_message                            */
/*****************************************************************************/

class kd_indented_message : public kdu_message {
  public: // Member functions
    kd_indented_message()
      { indent = 0; at_start_of_line = true; }
    virtual ~kd_indented_message() { return; }
    void set_indent(int num_spaces)
      { indent = num_spaces; }
    virtual void put_text(const char *string)
      { 
        for (; *string != '\0'; string++)
          { 
            if (at_start_of_line)
              issue_prefix();
            putc(*string,stdout);
            if (*string == '\n')
              at_start_of_line = true;
          }
      }
      /* Overrides `kdu_message::put_text'.  Passes text through to stdout,
         except that each line is indented by the amount specified by the
         most recent call to `set_indent'. */
    virtual void flush(bool end_of_message)
      { 
        if (end_of_message && !at_start_of_line)
          { putc('\n',stdout); at_start_of_line = true; }
      }
      /* Overrides `kdu_message::flush'.  Does nothing unless `end_of_message'
         is true, in which case the function terminates any currently
         unterminated line. */
  private: // Helper functions
    void issue_prefix()
      { 
        assert(at_start_of_line);
        for (int s=indent; s > 0; s--)
          putc(' ',stdout);
        at_start_of_line = false;
      }
  private: // Data
    int indent;
    bool at_start_of_line;
  };
  /* Notes:
       This object just passes all text through to stdout, except that
     each line is prefixed by an indentation string, as set by calls to
     `set_indent'. */

#endif // JP2INFO_LOCAL_H
