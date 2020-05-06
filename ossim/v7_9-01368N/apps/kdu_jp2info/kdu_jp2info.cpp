/*****************************************************************************/
// File: kdu_jp2info.cpp [scope = APPS/JP2INFO]
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
   Small application which reads a JP2-family file, parses its box structure
and prints a summary of the information found to standard output.
******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "kdu_messaging.h"
#include "kdu_utils.h"
#include "kdu_compressed.h"
#include "kdu_params.h"
#include "kdu_args.h"
#include "kdu_file_io.h"
#include "jp2.h"
#include "mj2.h"
#include "jpx.h"
#include "jpb.h"
#include "jp2info_local.h"

using namespace kdu_supp; // Also includes the `kdu_core' namespace

/* ========================================================================= */
/*                         Set up messaging services                         */
/* ========================================================================= */

class kdu_stream_message : public kdu_message {
  public: // Member classes
    kdu_stream_message(FILE *dest)
      { this->dest = dest; }
    void start_message()
      { fprintf(dest,"-------------\n"); }
    void put_text(const char *string)
      { fprintf(dest,"%s",string); }
    void flush(bool end_of_message=false)
      { fflush(dest); }
  private: // Data
    FILE *dest;
  };

static kdu_stream_message cout_message(stdout);
static kdu_stream_message cerr_message(stderr);
static kdu_message_formatter pretty_cout(&cout_message);
static kdu_message_formatter pretty_cerr(&cerr_message);


/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                        print_version                               */
/*****************************************************************************/

static void
print_version()
{
  kdu_message_formatter out(&cout_message);
  out.start_message();
  out << "This is Kakadu's \"kdu_jp2info\" application.\n";
  out << "\tCompiled against the Kakadu core system, version "
      << KDU_CORE_VERSION << "\n";
  out << "\tCurrent core system version is "
      << kdu_get_core_version() << "\n";
  out.flush(true);
  exit(0);
}

/*****************************************************************************/
/* STATIC                        print_usage                                 */
/*****************************************************************************/

static void
  print_usage(char *prog, bool comprehensive)
{
  kdu_message_formatter out(&cout_message);
  
  out << "Usage:\n  \"" << prog << " ...\n";
  out.set_master_indent(3);
  out << "-i <input file>\n";
  if (comprehensive)
    out << "\tMust be a JP2-family file (JP2, JPX or JPM), an "
           "elementary broadcast stream (JPB file, as consumed by "
           "\"kdu_v_expand\" or written by \"kdu_v_compress\"), or a"
           "raw JPEG2000 codestream.  The file suffix is not important, "
           "since this application auto-detects the file type.\n";
  out << "-boxes <max len>\n";
  if (comprehensive)
    out << "\tThis option requests the application to print a comprehensive "
           "description of the box structure to standard output.  The "
           "option has no impact on raw codestream files.  For JP2-family "
           "files (JP2, JPX or JPM), if this option is not supplied, only "
           "the first two boxes (signature box and file-type box) will "
           "be expanded.  The argument takes a single non-negative "
           "integer parameter that affects the way in which certain "
           "box types may be expanded.  If the value of this parameter "
           "is 0, only the box type, location and size will be printed, "
           "which is identical to the default behaviour when the `-boxes' "
           "argument is not supplied at all.  Apart from this, the parameter "
           "is usually interpreted as a limit on the number of bytes "
           "of each box's contents that are expanded, but many simple box "
           "types are expanded in full regardless of this limit.\n";
  out << "-hex <max len>\n";
  if (comprehensive)
    out << "\tThis option requests a hex dump for the contents of boxes "
           "that are not expanded into meaningful descriptions, either "
           "because the `-boxes' argument was not supplied (or had "
           "parameter 0) or because no internal textualization facility "
           "exists for the given box-type.  The argument takes a single "
           "positive integer parameter that is interpreted as a limit on "
           "the number of bytes to be expanded in the hex dump, but "
           "the internal implementation rounds this up to a multiple "
           "of 16, which is the number of values printed on each line.\n";
  out << "-siz -- print a full description of each codestream's SIZ info\n";
  if (comprehensive)
    out << "\tThis option applies both to raw codestreams and to contiguous "
           "codestream boxes found in JP2-family files (not just at the top "
           "level of the file).  If this option is omitted, the application "
           "only prints the codestream canvas dimensions, number of "
           "image components and number of tiles for each such codestream.  "
           "This option requests this summary information to be augmented by "
           "a complete record of the contents of the SIZ marker segment "
           "found within each relevant codestream.\n";
  out << "-com -- print comment marker segments\n";
  if (comprehensive)
    out << "\tThis option applies both to raw codestreams and to contiguous "
           "codestream boxes found in JP2-family files (not just at the top "
           "level of the file).  If present, the contents of any comment "
           "marker segment found in the main header of a codestream will "
           "be included in the generated output.\n";
  out << "-version -- print core system version I was compiled against.\n";
  out << "-v -- abbreviation of `-version'\n";
  out << "-usage -- print a comprehensive usage statement.\n";
  out << "-u -- print a brief usage statement.\"\n\n";
  out.flush();
  exit(0);
}

/*****************************************************************************/
/* STATIC                       parse_arguments                              */
/*****************************************************************************/

static char *
  parse_arguments(kdu_args &args, int &box_limit, int &hex_limit,
                  bool &expand_siz, bool &expand_com)
  /* Returns the input file name as a dynamically allocated string.  The
     `box_limit' argument is set to 0 if we want only the box skeleton,
     and a positive (max-len) value if we want a full description of all
     boxes.  The `hex_limit' argument is set to 0 unless the `-hex'
     argument appears. */
{
  box_limit = 0;
  hex_limit = 0;
  expand_siz = false;
  expand_com = false;
  char *fname = NULL;
  
  if ((args.get_first() == NULL) || (args.find("-u") != NULL))
    print_usage(args.get_prog_name(),false);
  if (args.find("-usage") != NULL)
    print_usage(args.get_prog_name(),true);
  if ((args.find("-version") != NULL) || (args.find("-v") != NULL))
    print_version();

  if (args.find("-i") != NULL)
    { 
      char *string = args.advance();
      if ((string == NULL) || (*string == '\0'))
        { kdu_error e; e << "The `-i' argument requires a non-empty "
          "string parameter (the input filename).\n"; }
      fname = new char[strlen(string)+1];
      strcpy(fname,string);
      args.advance();
    }
  else
    { kdu_error e; e << "You must supply an input filename via the "
      "\"-i\" argument."; }
  if (args.find("-boxes") != NULL)
    { 
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&box_limit) == 0) ||
          (box_limit < 0))
        { kdu_error e; e << "The `-boxes' argument requires a non-negative "
          "integer parameter."; }
      args.advance();
    }
  if (args.find("-hex") != NULL)
    { 
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&hex_limit) == 0) ||
          (hex_limit <= 0))
        { kdu_error e; e << "The `-hex' argument requires a positive "
          "integer parameter."; }
      args.advance();
    }
  if (args.find("-siz") != NULL)
    { 
      expand_siz = true;
      args.advance();
    }
  if (args.find("-com") != NULL)
    { 
      expand_com = true;
      args.advance();
    }
  
  return fname;
}

/*****************************************************************************/
/* STATIC INLINE                   make_hex                                  */
/*****************************************************************************/

static inline char make_hex(kdu_byte val)
{
  val &= 0x0F;
  if (val < 10)
    return (char) ('0'+val);
  else
    return (char) ('A'+val-10);
}

/*****************************************************************************/
/* STATIC                      generate_hex_dump                             */
/*****************************************************************************/

static bool
  generate_hex_dump(jp2_input_box *box, kdu_message &msg, int max_len)
{
  kdu_byte row_buf[16];
  int num_rows=0, row_bytes, c;
  while ((max_len > 0) && ((row_bytes = box->read(row_buf,16)) > 0))
    { 
      char row_text[80], *cp;
      for (cp=row_text, c=0; c < row_bytes; c++)
        { 
          if (c != 0)
            *(cp++) = ' ';
          *(cp++) = make_hex(row_buf[c]>>4);
          *(cp++) = make_hex(row_buf[c]);
        }
      *(cp++) = '\n';
      *cp = '\0';
      msg << row_text;
      max_len -= row_bytes;
    }
  return (num_rows > 0);
}

/*****************************************************************************/
/* STATIC                      process_codestream                            */
/*****************************************************************************/

static void
  process_codestream(kdu_compressed_source *src, kd_indented_message &msg,
                     int entry_indent, bool expand_siz, bool expand_com)
{
  kdu_byte word_buf[2];
  kdu_uint16 marker=0;
  int body_length=0;
  kdu_byte *body_buf = NULL;
  try { 
    // Start by reading the SIZ marker segment
    if ((src->read(word_buf,2) != 2) || (word_buf[0] != 0xFF) ||
        ((marker=((kdu_uint16) 0xFF00)+word_buf[1]) != KDU_SIZ))
      { kdu_error e;
        e << "Failed to find expected SIZ marker in codestream."; }
    if ((src->read(word_buf,2) != 2) ||
        ((body_length = ((((int) word_buf[0]) << 8) + word_buf[1] - 2)) < 1))
      { kdu_error e; e << "SIZ marker segment for codestream has 0 length."; }
    body_buf = new kdu_byte[body_length];
    if (src->read(body_buf,body_length) != body_length)
      { kdu_error e;
        e << "SIZ marker segment appears to have been truncated."; }
    siz_params params;
    kdu_params *p_ref = &params;
    p_ref->translate_marker_segment(KDU_SIZ,body_length,body_buf,-1,0);
    kdu_coords image_origin, canvas_size, tile_origin, tile_size;
    int num_components;
    p_ref->get(Sorigin,0,0,image_origin.y);
    p_ref->get(Sorigin,0,1,image_origin.x);
    p_ref->get(Ssize,0,0,canvas_size.y);
    p_ref->get(Ssize,0,1,canvas_size.x);
    p_ref->get(Scomponents,0,0,num_components);
    p_ref->get(Stile_origin,0,0,tile_origin.y);
    p_ref->get(Stile_origin,0,1,tile_origin.x);
    p_ref->get(Stiles,0,0,tile_size.y);
    p_ref->get(Stiles,0,1,tile_size.x);
    kdu_coords image_size = canvas_size - image_origin;
    msg.set_indent(entry_indent+2);
    msg << "<width> " << image_size.x << " </width>\n";
    msg << "<height> " << image_size.y << " </height>\n";
    msg << "<components> " << num_components << " </components>\n";
    msg << "<tiles> "
        << (ceil_ratio(canvas_size.x-tile_origin.x,tile_size.x) *
            ceil_ratio(canvas_size.y-tile_origin.y,tile_size.y))
        << " </tiles>\n";
    if (expand_siz)
      { 
        msg << "<SIZ>\n";
        msg.set_indent(entry_indent+4);
        p_ref->textualize_attributes(msg);
        msg.set_indent(entry_indent+2);
        msg << "</SIZ>\n";
      }
    delete[] body_buf; body_buf = NULL;
    
    // Now see if we have to walk through the other main header marker
    // segments looking for COM segments
    while (expand_com && (src->read(word_buf,2) == 2) && (word_buf[0]==0xFF) &&
           ((marker = ((kdu_uint16) 0xFF00)+word_buf[1]) != KDU_SOT) &&
           (marker != KDU_EOC))
      { 
        if (src->read(word_buf,2) != 2)
          break; // Marker segment truncated but don't bother reporting it
        body_length = ((int) word_buf[0]) + word_buf[1] - 2;
        if (body_length < 0)
          break; // Malformed marker segment but don't bother reporting it
        if ((marker != KDU_COM) || (body_length < 3))
          src->seek(src->get_pos()+body_length); // Skip over the marker seg
        else
          { // Read the comment marker segment
            body_buf = new kdu_byte[body_length+1];
            if (src->read(body_buf,body_length) != body_length)
              { kdu_error e;
                e << "COM marker segment appears to have been truncated."; }
            body_buf[body_length] = '\0'; // In case we need to print as string
            int rcom = ((int) body_buf[0]) + body_buf[1];
            if (rcom == 0)
              msg << "<COM type=\"binary\" length=\""
                  << (body_length-2) << "\"></COM>\n";
            if (rcom == 1)
              { 
                msg << "<COM type=\"Latin\" length=\""
                    << (body_length-2) << "\">\n";
                msg.set_indent(entry_indent+4);
                msg << "<![CDATA[\n";
                msg << (char *)(body_buf+2);
                msg << "\n]]>\n";
                msg.set_indent(entry_indent+2);
                msg << "</COM>\n";
              }
            else
              msg << "<COM type=\"" << rcom << "\" length=\""
                  << (body_length-2) << "\"></COM>\n";                
            delete[] body_buf; body_buf = NULL;
          }
      }
  } catch (...) {
    if (body_buf != NULL)
      delete[] body_buf;
    throw;
  }
  msg.flush(true);
}

/*****************************************************************************/
/* STATIC                         process_box                                */
/*****************************************************************************/

static void
  process_box(jp2_input_box &box, kd_indented_message &msg,
              jp2_box_textualizer &textualizer,
              int entry_indent, int box_limit, int hex_limit,
              bool expand_siz, bool expand_com)
  /* This function expects `box' to be an open box that is to be processed,
     along with all of its sub-boxes.  Before returning, the function
     closes the box. */
{
  kdu_uint32 box_type = box.get_box_type();
  char box_signature[5] = {'\0','\0','\0','\0','\0'};
  jp2_4cc_to_string(box_type,box_signature);
  jp2_locator loc = box.get_locator();
  kdu_long box_pos = loc.get_file_pos();
  int header_len = box.get_box_header_length();
  kdu_long body_len = box.get_remaining_bytes();
  const char *box_name = textualizer.get_box_name(box_type);
  bool can_expand = ((box_limit > 0) &&
                     textualizer.check_textualizer_function(box_type));
  
  msg.set_indent(entry_indent+2);
  const char *element_name;
  if (box_name != NULL)
    { // XML element name comes from box signature 
      element_name = box_signature;
      msg << "<" << element_name << " name=\"" << box_name << " box\" ";
    }
  else
    { // XML element name become "unknown_box"
      element_name = "unknown_box";
      char type_hex[10];
      sprintf(type_hex,"%08X",box_type);
      msg << "<unknown_box type_4cc=\"" << box_signature
          << "\" type_hex=\"" << type_hex << "\" ";
    }
  msg << "header=\"" << header_len << "\" body=\"";
  if (body_len < 0)
    msg << "rubber";
  else
    msg << body_len;
  msg << "\" pos=\"" << box_pos << "\">";
  
  if (box_type == jp2_codestream_4cc)
    { 
      msg << "\n";
      kdu_byte soc_buf[2] = {0,0};
      box.read(soc_buf,2);
      kdu_uint16 soc_code = (((kdu_uint16) soc_buf[0])<<8) + soc_buf[1];
      if (soc_code != KDU_SOC)
        { kdu_error e; e << "Codestream does not commence with SOC marker."; }
      msg.set_indent(entry_indent+4);
      msg << "<codestream>\n";
      process_codestream(&box,msg,entry_indent+4,expand_siz,expand_com);
      msg.set_indent(entry_indent+4);
      msg << "</codestream>\n";
    }
  else if (jp2_is_superbox(box_type))
    { 
      jp2_input_box sub_box;
      sub_box.open(&box);
      if (sub_box.exists())
        { 
          msg << "\n";
          while (sub_box.exists())
            { 
              process_box(sub_box,msg,textualizer,
                          entry_indent+2,box_limit,hex_limit,
                          expand_siz,expand_com);
              sub_box.open_next();
            }
        }
    }
  else if ((hex_limit > 0) || can_expand)
    { // Use the `textualizer'
      msg << "\n";
      msg.set_indent(entry_indent+4);
      if ((!can_expand) ||
          ((!textualizer.textualize_box(&box,msg,true,box_limit)) &&
           (hex_limit > 0)))
        { 
          msg << "<hexdump>\n";
          msg.set_indent(entry_indent+6);
          generate_hex_dump(&box,msg,hex_limit);
          msg.set_indent(entry_indent+4);
          msg << "</hexdump>\n";
        }
      kdu_long remaining_bytes = box.get_remaining_bytes();
      if (remaining_bytes > 0)
        msg << "<unexpanded bytes=\"" << remaining_bytes << "\" />\n";
    }
  
  msg.set_indent(entry_indent+2);
  msg << "</" << element_name << ">\n";
  box.close();
}


/* ========================================================================= */
/*                             External Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                           main                                     */
/*****************************************************************************/

int
  main(int argc, char *argv[])
{
  // Prepare console I/O and argument parsing.
  kdu_customize_warnings(&pretty_cout);
  kdu_customize_errors(&pretty_cerr);
  kdu_args args(argc,argv,"-s");
  
  // Collect arguments
  char *fname=NULL;
  int box_limit=0, hex_limit=0;
  bool expand_siz=false, expand_com=false;
  fname = parse_arguments(args,box_limit,hex_limit,expand_siz,expand_com);
  if (args.show_unrecognized(pretty_cout) != 0)
    { kdu_error e; e << "There were unrecognized command line arguments!"; }
  
  // Configure textualization machinery
  jp2_box_textualizer textualizer;
  jp2_add_box_descriptions(textualizer);
  jpx_add_box_descriptions(textualizer);
  mj2_add_box_descriptions(textualizer);
  jpb_add_box_descriptions(textualizer);
  
  // Process the input file
  kd_indented_message msg;
  kdu_simple_file_source file_src;
  
  kdu_byte soc_buf[2];
  file_src.open(fname); // Fails if file cannot be opened at all
  if (file_src.read(soc_buf,2) != 2)
    { kdu_error e; e << "Input file contains less than 2 bytes!!"; }
  kdu_uint16 soc_code = (((kdu_uint16) soc_buf[0])<<8) + soc_buf[1];
  if (soc_code == KDU_SOC)
    { // Process as raw codestream
      msg << "\n<codestream>\n";
      process_codestream(&file_src,msg,0,expand_siz,expand_com);
      msg.set_indent(0);
      msg << "</codestream>\n";
      file_src.close();
    }
  else
    { // Process as a box-structured file
      file_src.close();
      jp2_family_src src;
      src.open(fname,true); // Fails if file cannot be opened at all
      jp2_input_box box;
      bool processed = false;
      if (box.open(&src))
        { 
          if (box.get_box_type() == jpb_elementary_stream_4cc)
            { // Process as an elementary broadcast stream
              msg << "\n<elementary_broadcast_stream>\n";
              do {
                process_box(box,msg,textualizer,0,box_limit,hex_limit,
                            expand_siz,expand_com);
              } while (box.open_next());
              msg.set_indent(0);
              msg << "</elementary_broadcast_stream>\n";
              processed = true;
            }
          else if (box.get_box_type() == jp2_signature_4cc)
            { // Treat as a regular JP2-family file
              box.close();
              box.open_next();
              if (box.get_box_type() != jp2_file_type_4cc)
                { kdu_error e; e << "JP2 family source is missing file-type "
                  "box -- expected immediately after the signature box."; }
              msg << "\n<JP2_family_file>\n";
              process_box(box,msg,textualizer,0,INT_MAX,0,false,false);
              while (box.open_next())
                process_box(box,msg,textualizer,0,box_limit,hex_limit,
                            expand_siz,expand_com);
              msg.set_indent(0);
              msg << "</JP2_family_file>\n";
              processed = true;
            }
        }
      src.close();
      if (!processed)
        { kdu_error e; e << "Input file is neither a raw codestream nor a "
          "box-structured file.  Not a JPEG2000 file."; }
    }
  
  // Cleanup
  delete[] fname;
  
  return 0;
}
