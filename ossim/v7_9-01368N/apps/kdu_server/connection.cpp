/*****************************************************************************/
// File: connection.cpp [scope = APPS/SERVER]
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
   Implements the session connection machinery, which talks HTTP with the
client in order to return the parameters needed to establish a persistent
connection -- it also brokers the persistent connection process itself
handing off the relevant sockets once connected.  This source file forms part
of the `kdu_server' application.
******************************************************************************/

#include <math.h>
#include "server_local.h"
#include "kdu_messaging.h"
#include "kdu_utils.h"

using namespace kdu_supp; // Also includes the `kdu_core' namespace

static int kds_ack_delay_lut_x256[8] =
  {                         256,  (int)(256.0*pow(2.0,1.125)),
    (int)(256.0*pow(2.0,1.250)),  (int)(256.0*pow(2.0,1.375)),
    (int)(256.0*pow(2.0,1.500)),  (int)(256.0*pow(2.0,1.625)),
    (int)(256.0*pow(2.0,1.750)),  (int)(256.0*pow(2.0,1.875)) };


//#define KD_PRINT_CHANNEL_STATE_UPDATES

/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                  process_delegate_response                         */
/*****************************************************************************/

static int
  process_delegate_response(const char *reply, kdcs_message_block &par_block,
                            kdcs_sockaddr &address)
  /* This function essentially copies the `reply' paragraph from a delegated
     host server into the supplied `par_block', returning the value of the
     status code received from the delegated host.  However, it also
     introduces a number of modifications.
        Firstly, if the delegated host's reply did not include the
     "Connection: close" header, that header will be appended to the
     paragraph written to `par_block'.
        Secondly, if the reply paragraph includes a "JPIP-cnew:" header, the
     new channel details are scanned and the host address and port
     information are included in the list if not already in there.
        If `reply' is not a valid HTTP reply paragraph, the function returns
     error code 503, which allows the delegation function to try another
     host. */
{
  int code = 503;
  const char *end, *cp;
  const char *host_address = address.textualize(KDCS_ADDR_FLAG_LITERAL_ONLY);
  kdu_uint16 port = address.get_port();
  
  if (reply == NULL)
    return code;
  cp = strchr(reply,' ');
  if (cp == NULL)
    return code;
  while (*cp == ' ') cp++;
  if (sscanf(cp,"%d",&code) == 0)
    return code;

  par_block.restart();
  bool have_connection_close = false;
  while ((*reply != '\0') && (*reply != '\n'))
    {
      for (end=reply; (*end != '\n') && (*end != '\0'); end++);
      int line_length = (int)(end-reply);
      if (kdcs_has_caseless_prefix(reply,"JPIP-" JPIP_FIELD_CHANNEL_NEW ": "))
        {
          reply += strlen("JPIP-" JPIP_FIELD_CHANNEL_NEW ": ");
          while (*reply == ' ') reply++;
          par_block << "JPIP-" JPIP_FIELD_CHANNEL_NEW ": ";
          while (1)
            { // Scan through all the comma-delimited tokens
              cp = reply;
              while ((*cp != ',') && (*cp != ' ') &&
                     (*cp != '\0') && (*cp != '\n'))
                cp++;
              if (cp > reply)
                {
                  if (kdcs_has_caseless_prefix(reply,"host="))
                    host_address = NULL;
                  else if (kdcs_has_caseless_prefix(reply,"port="))
                    port = 0;
                  par_block.write_raw((kdu_byte *) reply,(int)(cp-reply));
                }
              if (*cp != ',')
                break;
              par_block << ",";
              reply = cp+1;
            }
          if (host_address != NULL)
            par_block << ",host=" << host_address;
          if (port != 0)
            par_block << ",port=" << port;
          par_block << "\r\n";
        }
      else
        {
          if (kdcs_has_caseless_prefix(reply,"Connection: "))
            {
              cp = strchr(reply,' ');
              while (*cp == ' ') cp++;
              if (kdcs_has_caseless_prefix(cp,"close"))
                have_connection_close = true;
            }
          par_block.write_raw((kdu_byte *) reply,line_length);
          par_block << "\r\n";
        }
      reply = (*end == '\0')?end:(end+1);
    }
  if (!have_connection_close)
    par_block << "Connection: close\r\n";
  par_block << "\r\n";
  return code;
}


/* ========================================================================= */
/*                              kd_request_fields                            */
/* ========================================================================= */

/*****************************************************************************/
/*                       kd_request_fields::write_query                      */
/*****************************************************************************/

void
  kd_request_fields::write_query(kdcs_message_block &block) const
{
  int n=0;

  if (target != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_TARGET "=" << target;
  if (sub_target != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_SUB_TARGET "="<<sub_target;
  if (target_id != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_TARGET_ID "="<<target_id;

  if (channel_id != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_CHANNEL_ID "="<<channel_id;
  if (channel_new != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_CHANNEL_NEW "="<<channel_new;
  if (channel_close != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_CHANNEL_CLOSE "="<<channel_close;
  if (request_id != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_REQUEST_ID "="<<request_id;

  if (full_size != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_FULL_SIZE "="<<full_size;
  if (region_size != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_REGION_SIZE "="<<region_size;
  if (region_offset != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_REGION_OFFSET "="<<region_offset;
  if (components != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_COMPONENTS "="<<components;
  if (codestreams != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_CODESTREAMS "="<<codestreams;
  if (contexts != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_CONTEXTS "="<<contexts;
  if (roi != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_ROI "="<<roi;
  if (layers != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_LAYERS "="<<layers;
  if (source_rate != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_SOURCE_RATE "="<<source_rate;

  if (meta_request != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_META_REQUEST "="<<meta_request;

  if (max_length != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_MAX_LENGTH "="<<max_length;
  if (max_quality != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_MAX_QUALITY "="<<max_quality;

  if (align != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_ALIGN "="<<align;
  if (timed_wait != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_TIMED_WAIT "="<<timed_wait;
  if (type != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_TYPE "="<<type;
  if (delivery_rate != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_DELIVERY_RATE "="<<delivery_rate;
  if (chunk_abandon != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_CHUNK_ABANDON "="<<chunk_abandon;
  if (barrier_id != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_BARRIER_ID "="<<barrier_id;
  if (send_to != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_SEND_TO "="<<send_to;

  if (model != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_MODEL "="<<model;
  if (tp_model != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_TP_MODEL "="<<tp_model;
  if (need != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_NEED "="<<need;

  if (capabilities != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_CAPABILITIES "="<<capabilities;
  if (preferences != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_PREFERENCES "="<<preferences;
  if (handled_requests != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_HANDLED_REQUESTS "="
                                <<handled_requests;

  if (upload != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_UPLOAD "="<<upload;
  if (xpath_box != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_XPATH_BOX "="<<xpath_box;
  if (xpath_bin != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_XPATH_BIN "="<<xpath_bin;
  if (xpath_query != NULL)
    block << (((n++)==0)?"":"&")<<JPIP_FIELD_XPATH_QUERY "="<<xpath_query;

  if (unrecognized != NULL)
    block << (((n++)==0)?"":"&") << unrecognized;
}


/* ========================================================================= */
/*                                 kd_request                                */
/* ========================================================================= */

/*****************************************************************************/
/*                          kd_request::write_method                         */
/*****************************************************************************/

void
  kd_request::write_method(const char *string, int string_len)
{
  assert(string != NULL);
  resource = query = http_accept = NULL;
  set_cur_buf_len(string_len+1);
  memcpy(buf,string,string_len); buf[string_len]='\0';
  method = buf;
}

/*****************************************************************************/
/*                         kd_request::write_resource                        */
/*****************************************************************************/

void
  kd_request::write_resource(const char *string, int string_len)
{
  assert((string != NULL) && (method != NULL));
  int offset = cur_buf_len;
  set_cur_buf_len(offset+string_len+1);
  method = buf;
  memcpy(buf+offset,string,string_len); buf[offset+string_len]='\0';
  resource = buf+offset;
}

/*****************************************************************************/
/*                          kd_request::write_query                          */
/*****************************************************************************/

void
  kd_request::write_query(const char *string, int string_len)
{
  assert((string != NULL) && (method != NULL) && (resource != NULL));
  char *old_buf = buf;
  int offset = cur_buf_len;
  set_cur_buf_len(offset+string_len+1);
  method = buf;
  resource = buf + (resource-old_buf);
  if (http_accept != NULL)
    http_accept = buf + (http_accept-old_buf);
  memcpy(buf+offset,string,string_len); buf[offset+string_len]='\0';
  query = buf+offset;
}

/*****************************************************************************/
/*                       kd_request::write_http_accept                       */
/*****************************************************************************/

void
  kd_request::write_http_accept(const char *string, int string_len)
{
  assert((string != NULL) && (method != NULL) && (resource != NULL));
  char *old_buf = buf;
  int offset = cur_buf_len;
  set_cur_buf_len(offset+string_len+1);
  method = buf;
  resource = buf + (resource-old_buf);
  if (query != NULL)
    query = buf + (query-old_buf);
  memcpy(buf+offset,string,string_len); buf[offset+string_len]='\0';
  http_accept = buf+offset;
}

/*****************************************************************************/
/*                             kd_request::copy                              */
/*****************************************************************************/

void
  kd_request::copy(const kd_request *src)
{
  init();
  set_cur_buf_len(src->cur_buf_len);
  memcpy(buf,src->buf,cur_buf_len);
  if (src->method != NULL)
    method = buf + (src->method - src->buf);
  if (src->resource != NULL)
    resource = buf + (src->resource - src->buf);
  const char **src_ptr = (const char **) &(src->fields);
  const char **dst_ptr = (const char **) &fields;
  const char **lim_src_ptr = (const char **)(&(src->fields)+1);
  for (; src_ptr < lim_src_ptr; src_ptr++, dst_ptr++)
    { // Walk through all the request fields
      if (*src_ptr != NULL)
        {
          assert((*src_ptr >= src->buf) &&
                 (*src_ptr < (src->buf+src->cur_buf_len)));
          *dst_ptr = buf + (*src_ptr - src->buf);
        }
      else
        *dst_ptr = NULL;
    }
  close_connection = src->close_connection;
  preemptive = src->preemptive;
}


/* ========================================================================= */
/*                              kd_request_queue                             */
/* ========================================================================= */

/*****************************************************************************/
/*                     kd_request_queue::~kd_request_queue                   */
/*****************************************************************************/

kd_request_queue::~kd_request_queue()
{
  while ((tail=head) != NULL)
    {
      head=tail->next;
      delete tail;
    }
  while ((tail=free_list) != NULL)
    {
      free_list=tail->next;
      delete tail;
    }
  if (pending != NULL)
    delete pending;
}

/*****************************************************************************/
/*                           kd_request_queue::init                          */
/*****************************************************************************/

void
  kd_request_queue::init()
{
  while ((tail=head) != NULL)
    {
      head = tail->next;
      return_request(tail);
    }
  if (pending != NULL)
    return_request(pending);
  pending = NULL;
  pending_body_bytes = 0;
}

/*****************************************************************************/
/*                       kd_request_queue::read_request                      */
/*****************************************************************************/

bool
  kd_request_queue::read_request(kdcs_tcp_channel *channel)
{
  const char *cp, *text;
  
  if (pending_body_bytes == 0)
    {
      assert(pending == NULL);
      do {
          if ((text = channel->read_paragraph()) == NULL)
            return false;
          kd_msg_log.print(text,"<< ");
        } while (*text == '\n'); // Skip over blank lines

      pending = get_empty_request();

      if (kdcs_has_caseless_prefix(text,"GET "))
        {
          pending->write_method(text,3);
          text += 4;
        }
      else if (kdcs_has_caseless_prefix(text,"POST "))
        {
          pending->write_method(text,4);
          text += 5;
          if ((cp = kdcs_caseless_search(text,"\nContent-length:")) != NULL)
            {
              while (*cp == ' ') cp++;
              sscanf(cp,"%d",&pending_body_bytes);
              if ((cp = kdcs_caseless_search(text,"\nContent-type:")) != NULL)
                {
                  while (*cp == ' ') cp++;
                  if (!(kdcs_has_caseless_prefix(cp,
                                      "application/x-www-form-urlencoded") ||
                        kdcs_has_caseless_prefix(cp,"x-www-form-urlencoded")))
                    cp = NULL;
                }
            }
          if ((cp == NULL) || (pending_body_bytes < 0) ||
              (pending_body_bytes > 32000))
            { // Protocol not being followed; ignore query and kill connection
              pending_body_bytes = 0;
              pending->close_connection = true;        
            }
        }
      else
        { // Copy the method string only
          for (cp=text; (*cp != ' ') && (*cp != '\n') && (*cp != '\0'); cp++);
          pending->write_method(text,(int)(cp-text));
          pending->close_connection = true;
          complete_pending_request();
          return true; // Return true, even though it is an invalid request
        }

      // Find start of the resource string
      while (*text == ' ') text++;
      if (*text == '/')
        text++;
      else
        for (; (*text != ' ') && (*text != '\0') && (*text != '\n'); text++)
          if ((text[0] == '/') && (text[1] == '/'))
            { text += 2; break; }

      // Find end of the resource string
      cp = text;
      while ((*cp != '?') && (*cp != '\0') && (*cp != ' ') && (*cp != '\n'))
        cp++;
      pending->write_resource(text,(int)(cp-text));
      text = cp;

      // Look for query string
      if (pending_body_bytes == 0)
        {
          if (*text == '?')
            {
              text++; cp = text;
              while ((*cp != ' ') && (*cp != '\n') && (*cp != '\0'))
                cp++;
            }
          pending->write_query(text,(int)(cp-text));
          text = cp;
        }
      
      if (!pending->close_connection)
        { // See if we need to close the connection
          while (*text == ' ') text++;
          if (kdcs_has_caseless_prefix(text,"HTTP/"))
            { 
              float version=0.0f, frac_scale=0.1f;
              const char *version_cp=text+5;
              for (; isdigit(version_cp[0]); version_cp++)
                version = 10.0f*version + (int)(version_cp[0]-'0');
              if (version_cp[0] == '.')
                for (version_cp++; isdigit(version_cp[0]); version_cp++, frac_scale*=0.1f)
                  version += frac_scale * (int)(version_cp[0]-'0');
              if (version < 1.1)
                pending->close_connection = true;
            }
          if ((cp=kdcs_caseless_search(text,"\nConnection:")) != NULL)
            {
              while (*cp == ' ') cp++;
              if (kdcs_has_caseless_prefix(cp,"CLOSE"))
                pending->close_connection = true;
            }
        }

      if ((cp=kdcs_caseless_search(text,"\nAccept:")) != NULL)
        {
          while (*cp == ' ') cp++;
          for (text=cp; (*cp != ' ') && (*cp != '\n') && (*cp != '\0'); cp++);
          pending->write_http_accept(text,(int)(cp-text));
        }
    }

  if (pending_body_bytes > 0)
    {
      int n;
      kdu_byte *body = channel->read_raw(pending_body_bytes);
      if (body == NULL)
        return false;

      text = (const char *) body;
      for (cp=text, n=0; n < pending_body_bytes; n++, cp++)
        if ((*cp == ' ') || (*cp == '\n') || (*cp == '\0'))
          break;
      pending->write_query(text,n);
      kd_msg_log.print(text,n,"<< ");
    }

  complete_pending_request();
  return true;
}

/*****************************************************************************/
/*                         kd_request_queue::push_copy                       */
/*****************************************************************************/

void
  kd_request_queue::push_copy(const kd_request *src)
{
  kd_request *req = get_empty_request();
  req->copy(src);
  if (tail == NULL)
    head = tail = req;
  else
    tail = tail->next = req;
}

/*****************************************************************************/
/*                      kd_request_queue::transfer_state                     */
/*****************************************************************************/

void
  kd_request_queue::transfer_state(kd_request_queue *src)
{
  assert(this->pending == NULL);
  if (src->head != NULL)
    {
      if (this->tail == NULL)
        this->head = src->head;
      else
        this->tail->next = src->head;
      if (src->tail != NULL)
        this->tail = src->tail;
      src->head = src->tail = NULL;
    }
  if (src->pending != NULL)
    {
      this->pending = src->pending;
      this->pending_body_bytes = src->pending_body_bytes;
      src->pending = NULL;
      src->pending_body_bytes = 0;
    }
}

/*****************************************************************************/
/*                 kd_request_queue::have_preempting_request                 */
/*****************************************************************************/

bool kd_request_queue::have_preempting_request(const char *channel_id)
{
  kd_request *req;
  for (req=head; req != NULL; req=req->next)
    if ((req->fields.channel_id != NULL) &&
        (strcmp(req->fields.channel_id,channel_id) == 0) &&
        (req->fields.channel_new == NULL) && req->preemptive)
      return true;
  return false;
}

/*****************************************************************************/
/*                         kd_request_queue::pop_head                        */
/*****************************************************************************/

const kd_request *
  kd_request_queue::pop_head()
{
  kd_request *req = head;
  if (req != NULL)
    {
      if ((head = req->next) == NULL)
        tail = NULL;
      return_request(req);
    }
  return req;
}

/*****************************************************************************/
/*                kd_request_queue::complete_pending_request                 */
/*****************************************************************************/

void
  kd_request_queue::complete_pending_request()
{
  assert(pending != NULL);
  if (tail == NULL)
    head = tail = pending;
  else
    tail = tail->next = pending;
  pending = NULL;
  pending_body_bytes = 0;

  if (tail->resource != NULL)
    kdu_hex_hex_decode((char *) tail->resource);
  else
    return;

  tail->preemptive = true; // Until proven otherwise
  const char *field_sep, *scan;
  for (field_sep=NULL, scan=tail->query; scan != NULL; scan=field_sep)
    {
      if (scan == field_sep)
        scan++;
      if (*scan == '\0')
        break;
      for (field_sep=scan; *field_sep != '\0'; field_sep++)
        if (*field_sep == '&')
          break;
      if (*field_sep == '\0')
        field_sep = NULL;
      kdu_hex_hex_decode((char *)scan,field_sep); // Decodes the request field
                       // and inserts null-terminator at or before `field_sep'.
      if (kdcs_parse_request_field(scan,JPIP_FIELD_WAIT))
        {
          if (strcmp(scan,"yes")==0)
            tail->preemptive = false;
        }
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_TARGET))
        tail->fields.target = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_SUB_TARGET))
        tail->fields.sub_target = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_TARGET_ID))
        tail->fields.target_id = scan;

      else if (kdcs_parse_request_field(scan,JPIP_FIELD_CHANNEL_ID))
        tail->fields.channel_id = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_CHANNEL_NEW))
        tail->fields.channel_new = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_CHANNEL_CLOSE))
        tail->fields.channel_close = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_REQUEST_ID))
        tail->fields.request_id = scan;

      else if (kdcs_parse_request_field(scan,JPIP_FIELD_FULL_SIZE))
        tail->fields.full_size = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_REGION_SIZE))
        tail->fields.region_size = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_REGION_OFFSET))
        tail->fields.region_offset = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_COMPONENTS))
        tail->fields.components = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_CODESTREAMS))
        tail->fields.codestreams = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_CONTEXTS))
        tail->fields.contexts = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_ROI))
        tail->fields.roi = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_LAYERS))
        tail->fields.layers = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_SOURCE_RATE))
        tail->fields.source_rate = scan;

      else if (kdcs_parse_request_field(scan,JPIP_FIELD_META_REQUEST))
        tail->fields.meta_request = scan;

      else if (kdcs_parse_request_field(scan,JPIP_FIELD_MAX_LENGTH))
        tail->fields.max_length = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_MAX_QUALITY))
        tail->fields.max_quality = scan;

      else if (kdcs_parse_request_field(scan,JPIP_FIELD_ALIGN))
        tail->fields.align = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_TIMED_WAIT))
        tail->fields.timed_wait = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_TYPE))
        tail->fields.type = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_DELIVERY_RATE))
        tail->fields.delivery_rate = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_CHUNK_ABANDON))
        tail->fields.chunk_abandon = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_BARRIER_ID))
        tail->fields.barrier_id = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_SEND_TO))
        tail->fields.send_to = scan;

      else if (kdcs_parse_request_field(scan,JPIP_FIELD_MODEL))
        tail->fields.model = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_TP_MODEL))
        tail->fields.tp_model = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_NEED))
        tail->fields.need = scan;

      else if (kdcs_parse_request_field(scan,JPIP_FIELD_CAPABILITIES))
        tail->fields.capabilities = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_PREFERENCES))
        tail->fields.preferences = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_HANDLED_REQUESTS))
        tail->fields.handled_requests = scan;

      else if (kdcs_parse_request_field(scan,JPIP_FIELD_UPLOAD))
        tail->fields.upload = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_XPATH_BOX))
        tail->fields.xpath_box = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_XPATH_BIN))
        tail->fields.xpath_bin = scan;
      else if (kdcs_parse_request_field(scan,JPIP_FIELD_XPATH_QUERY))
        tail->fields.xpath_query = scan;

      else if (kdcs_parse_request_field(scan,"admin_key"))
        tail->fields.admin_key = scan;
      else if (kdcs_parse_request_field(scan,"admin_command"))
        tail->fields.admin_command = scan;
      else
        tail->fields.unrecognized = scan;
    }

  if (tail->fields.target == NULL)
    tail->fields.target = tail->resource;

  if ((tail->fields.type == NULL) && (tail->http_accept != NULL))
    { // Parse acceptable types from the HTTP "Accept:" header
      tail->fields.type = tail->http_accept;
      const char *sp=tail->http_accept;
      char *cp = (char *)(tail->fields.type);
      for (; *sp != '\0'; sp++)
        { // It is sufficient to remove spaces, since "type" request bodies
          // have otherwise the same syntax as the "Accept:" header.
          if ((*sp != ' ') && (*sp != '\r') && (*sp != '\n') && (*sp != '\t'))
            *(cp++) = *sp;
        }
      *cp = '\0';
    }
}


/* ========================================================================= */
/*                              kds_jpip_channel                             */
/* ========================================================================= */

/*****************************************************************************/
/*                     kds_jpip_channel::kds_jpip_channel                    */
/*****************************************************************************/

kds_jpip_channel::kds_jpip_channel(int min_suggested_chunk_size,
                                   int max_chunk_size,
                                   float max_bytes_per_second)
{
  assert(max_chunk_size >= min_suggested_chunk_size);
  assert(min_suggested_chunk_size > 8);
  assert(max_bytes_per_second > 0.0F);
  this->monitor = NULL;
  this->channel = NULL;
  this->tcp_channel = NULL;
  this->udp_channel = NULL;
  this->min_suggested_chunk_bytes = min_suggested_chunk_size;
  this->max_chunk_bytes = max_chunk_size;
  this->is_auxiliary = false;
  this->waiting_on_send = false;
  this->waiting_on_delivery_gate = false;
  this->waiting_to_transmit = false;
  this->original_max_bytes_per_second = max_bytes_per_second;
  this->max_bytes_per_second = max_bytes_per_second;
  this->allow_retransmit = false; // Configure this in `set_auxiliary'
  this->max_intrinsic_ack_delay = 1; // Doesn't matter what the value is yet

  free_list = holding_head = holding_tail = NULL;
  completed_head = completed_tail = NULL;
  transmitted_head = transmitted_tail = NULL;
  need_chunk_trailer = false;
  no_more_data_expected = false;
  next_chunk_qid16 = 0;
  next_chunk_seq_val = 0;
  holding_bytes = window_bytes = 0;
  
  first_elt_pending_abandonment = NULL;
  min_max_abandon_qid = -1;
  barrier_qid = -1;
  
  mutex.create();
  internal_event.create(false); // Auto-reset event might be a bit faster
  udp_ready_event = NULL;
  abandonment_event = NULL;
  request_ready_event = NULL;
  queue_ready_event = NULL;
  idle_ready_event = NULL;
  
  start_time = timer.get_ellapsed_microseconds();
  last_cur_time = start_time;
  service_run_time = -1;
  delivery_gate = start_time; // Can delivery data immediately if desired
  max_ack_offset = 10000; // Start at 10ms
  timeout_gate = -1; // No timeout set up yet
  replenishing_timeout = 0; // No replenishment for timeouts
  earliest_wakeup = latest_wakeup = -1; // No scheduled wakeup yet

  min_rtt_usecs = 0; // Means that no RTT's have yet been observed
  queueing_tgt_bytes = max_chunk_bytes * 4;
  estimated_network_rate = max_bytes_per_second;
  estimated_network_usecs_per_byte = 1000000.0f / estimated_network_rate;
  cur_throttle_usecs_per_byte = 1000000.0f / estimated_network_rate;
  estimated_network_delay_usecs = 10000; // Assume 10 milliseconds to start
  max_delivery_lag_usecs = 10000; // 10 milliseconds

  window_threshold = queueing_tgt_bytes;
  
  group_based_flow_ctl = false;
  group_cum_delta_t = 0.0;
  group_cum_delta_b = 0.0;
  
  dgram_overhead = 32; // Arbitrary value for now
  dgram_loss_count = 0;
  dgram_completed_byte_count = 0;
  calculate_suggested_epoch_and_chunk_sizes(true);
  
  total_transmitted_bytes = 0;
  total_acknowledged_bytes = 0;
  total_client_abandoned_bytes = 0;

  idle_start = -1;
  idle_usecs = 0;
  total_rtt_usecs = total_rtt_events = 0;
  
  next = NULL;  
}

/*****************************************************************************/
/*                       kds_jpip_channel::init (TCP)                        */
/*****************************************************************************/

void kds_jpip_channel::init(kdcs_channel_monitor *monitor,
                            kdcs_tcp_channel *t_channel)
{
  assert((this->monitor == NULL) && (this->tcp_channel == NULL) &&
         (this->udp_channel == NULL));
  this->monitor = monitor;
  this->channel = this->tcp_channel = t_channel;
  this->udp_channel = NULL;
  if (!(mutex.exists() && internal_event.exists()))
    { // In case we are all out of synchronization resources
      close_channel();
      return;
    }
  mutex.lock();
  t_channel->set_channel_servicer(this);
  monitor->synchronize_timing(timer);
  start_time = timer.get_ellapsed_microseconds();
  last_cur_time = idle_start = start_time;
  idle_usecs = 0;
  mutex.unlock();
}

/*****************************************************************************/
/*                       kds_jpip_channel::init (UDP)                        */
/*****************************************************************************/

void kds_jpip_channel::init(kdcs_channel_monitor *monitor,
                            kdcs_udp_channel *u_channel)
{
  assert((this->monitor == NULL) && (this->tcp_channel == NULL) &&
         (this->udp_channel == NULL));
  this->monitor = monitor;
  this->channel = this->udp_channel = u_channel;
  this->tcp_channel = NULL;
  if (!(mutex.exists() && internal_event.exists()))
    { // In case we are all out of synchronization resources
      close_channel();
      return;
    }
  mutex.lock();
  u_channel->set_channel_servicer(this);
  monitor->synchronize_timing(timer);
  start_time = timer.get_ellapsed_microseconds();
  last_cur_time = idle_start = start_time;
  idle_usecs = 0;

  kdcs_sockaddr address;
  u_channel->get_peer_address(address);
  if (address.test_ipv6())
    dgram_overhead = 48; // Packet header length for IPv6 datagrams
  else
    dgram_overhead = 28; // Packet header length for IPv4 datagrams
  dgram_loss_count = 0;
  dgram_completed_byte_count = 0;
  
  group_based_flow_ctl = false; // UDP never uses group-based flow control

  int assumed_mtu = 1500;
  int single_mtu_bound = assumed_mtu-dgram_overhead; // To avoid fragmentation
  if (min_suggested_chunk_bytes > single_mtu_bound)
    min_suggested_chunk_bytes = single_mtu_bound;
  if (max_chunk_bytes > single_mtu_bound)
    max_chunk_bytes = single_mtu_bound;
  
  calculate_suggested_epoch_and_chunk_sizes(true); // Update current chunk size
  mutex.unlock();
}

/*****************************************************************************/
/*                    kds_jpip_channel::get_local_address                    */
/*****************************************************************************/

bool kds_jpip_channel::get_local_address(kdcs_sockaddr &addr)
{
  bool success = false;
  mutex.lock();
  if (channel != NULL)
    success = channel->get_local_address(addr);
  mutex.unlock();
  return success;
}

/*****************************************************************************/
/*                    kds_jpip_channel::get_peer_address                     */
/*****************************************************************************/

bool kds_jpip_channel::get_peer_address(kdcs_sockaddr &addr)
{
  bool success = false;
  mutex.lock();
  if (channel != NULL)
    success = channel->get_peer_address(addr);
  mutex.unlock();
  return success;
}

/*****************************************************************************/
/*                   kds_jpip_channel::set_bandwidth_limit                   */
/*****************************************************************************/

void kds_jpip_channel::set_bandwidth_limit(float bw_limit)
{
  if ((bw_limit <= 0.0F) || (bw_limit > original_max_bytes_per_second))
    bw_limit = original_max_bytes_per_second;
  mutex.lock();
  this->max_bytes_per_second = bw_limit;
  float min_throttle_usecs_per_byte = 1000000.0f/max_bytes_per_second;
  if (cur_throttle_usecs_per_byte < min_throttle_usecs_per_byte)
    cur_throttle_usecs_per_byte = min_throttle_usecs_per_byte;
  calculate_suggested_epoch_and_chunk_sizes(is_auxiliary &&
                                            (total_rtt_events < 1));
  mutex.unlock();
}

/*****************************************************************************/
/*                       kds_jpip_channel::set_timeout                       */
/*****************************************************************************/

void kds_jpip_channel::set_timeout(float seconds,
                                   bool replenish_timeout_after_progress)
{
  mutex.lock();
  if (channel != NULL)
    { 
      last_cur_time = timer.get_ellapsed_microseconds();
      kdu_long interval = (kdu_long) ceil(seconds*1000000);
      timeout_gate = last_cur_time + interval;
      if  (seconds < 0.0F)
        timeout_gate = -1; // Disable timeouts
      else if (timeout_gate >= 0)
        { 
          if ((timeout_gate <= last_cur_time) || (channel == NULL))
            wake_all_blocked_calls();
          else
            { 
              replenishing_timeout = interval;
              if (!replenish_timeout_after_progress)
                replenishing_timeout = 0;
              if ((earliest_wakeup < 0) || (timeout_gate < earliest_wakeup))
                { 
                  earliest_wakeup = latest_wakeup = timeout_gate;
                  channel->schedule_wakeup(earliest_wakeup,latest_wakeup);
                }
            }
        }
    }
  mutex.unlock();
}

/*****************************************************************************/
/*                          kds_jpip_channel::close                          */
/*****************************************************************************/

bool kds_jpip_channel::close(bool send_queued_data_first)
{ 
  mutex.lock();
  if (send_queued_data_first)
    {
      kd_queue_elt *qelt, *qnext, *qprev=NULL;
      for (qelt=holding_head; qelt != NULL; qprev=qelt, qelt=qnext)
        { // Remove any elements from the holding queue which are still
          // waiting for a chunk of data ... assume this will never arrive
          qnext = qelt->next;
          if (qelt->reply_body_bytes < 0)
            {
              assert(qelt->chunk_data == NULL);
              if (qelt == holding_tail)
                holding_tail = qprev;
              else
                assert(0); // Incomplete elements should really be at the tail
              if (qprev == NULL)
                holding_head = qnext;
              else
                qprev->next = qnext;
              holding_bytes -= qelt->reply_bytes;
              return_to_free_list(qelt);
            }
        }
      while ((transmitted_head != NULL) || (holding_head != NULL))
        {
          if (channel == NULL)
            { // Should not happen if above lists are already non-empty
              close_channel(); // Empty above lists
              break;
            }
          if ((timeout_gate >= 0) && (last_cur_time >= timeout_gate))
            {
              mutex.unlock();
              return false;
            }
          idle_ready_event = &internal_event;
          internal_event.reset();
          internal_event.wait(mutex);
        }
    }
  
  close_channel();  // Moves all unclaimed chunks to the completed list for now
  mutex.unlock();
  return true;
}

/*****************************************************************************/
/*                      kds_jpip_channel::set_auxiliary                      */
/*****************************************************************************/

void kds_jpip_channel::set_auxiliary(float udp_max_intrinsic_delay,
                                     bool udp_allow_retransmit,
                                     bool prefer_group_based_flow_ctl)
{
  if (is_auxiliary)
    return;
  mutex.lock();
  
  // Start by waiting for the holding queue to clear, if necessary
  while (holding_head != NULL)
    {
      assert(holding_tail->reply_body_bytes >= 0); // Waiting for chunks????
      if (((timeout_gate >= 0) && (last_cur_time >= timeout_gate)) ||
          (channel == NULL))
        close_channel();
      else
        {
          idle_ready_event = &internal_event;
          internal_event.reset();
          internal_event.wait(mutex);
        }
    }
  requests.init(); // We won't be using the request queue
  
  // At this point, it is just possible that the channel has already expired,
  // but that doesn't stop us configuring it as an auxiliary channel.  We
  // will find out soon enough if it has closed.  
  max_intrinsic_ack_delay = 1 + (kdu_long)(1000000.0*udp_max_intrinsic_delay);
  if (max_intrinsic_ack_delay < 1)
    max_intrinsic_ack_delay = 1; // Always wait at least 1 microsecond!
  max_ack_offset = 10000; // Start at 10 milliseconds
  if (max_ack_offset > max_intrinsic_ack_delay)
    max_ack_offset = max_intrinsic_ack_delay;
  if (max_ack_offset < 1000)
    max_ack_offset = 1000; // Increasing beyond `max_intrinsic_ack_delay' makes
                           // no difference in practice, but good to remind
                           // ourselves that we will enforce a lower bound of
                           // 1ms during adaptation of `max_ack_offset'.
  this->allow_retransmit = udp_allow_retransmit;
  
  if ((tcp_channel != NULL) && prefer_group_based_flow_ctl)
    { 
      group_based_flow_ctl = true;
      
      estimated_network_delay_usecs = 10000; // Sart by assuming 10ms
      if (estimated_network_rate > 5000)
        estimated_network_rate = 5000;
      estimated_network_usecs_per_byte = 1000000.0f / estimated_network_rate;
      window_threshold = 1;
      cur_throttle_usecs_per_byte = estimated_network_usecs_per_byte;
      group_cum_delta_t = group_cum_delta_b = 0.0;
    }
  else
    { // Initialize the Vegas-like flow control algorithm
      group_based_flow_ctl = false;
      
      // Start by assuming a very small value for Lnom and a modest RTT so that
      // the estimated network rate will be around 10kB/s only (ratio between
      // window threshold and network delay).  As soon as we get our first
      // acknowledgement, we generally increase Lnom but also have a better
      // idea of the network delay.
      queueing_tgt_bytes = max_chunk_bytes * (1 + (1000 / max_chunk_bytes));
      window_threshold = queueing_tgt_bytes;
      estimated_network_delay_usecs = 100000; // Start by assuming 100 ms
    }
  calculate_suggested_epoch_and_chunk_sizes(true);
  
  is_auxiliary = true;
  mutex.unlock();
}

/*****************************************************************************/
/*                 kds_jpip_channel::complete_udp_connection                 */
/*****************************************************************************/

bool kds_jpip_channel::complete_udp_connection(const char *channel_id,
                                               kdu_event *event_to_signal)
{
  bool result = false;
  int msg_len, target_msg_len = 4 + (int) strlen(channel_id);
  kdcs_sockaddr peer_addr;
  mutex.lock();
  if (udp_channel != NULL)
    { 
      udp_ready_event = NULL; // Cancel any existing `udp_ready_event'
      while (!result)
        { 
          if (udp_channel->is_connected())
            result = true;
          else
            { 
              try {
                const char *msg = (const char *)
                  udp_channel->recv_msg(msg_len,target_msg_len,&peer_addr);
                if (msg == NULL)
                  { // Failed to read a message
                    if ((udp_ready_event=event_to_signal) != NULL)
                      break; // Return false, so caller can wait on event
                    if ((timeout_gate < 0) || (last_cur_time < timeout_gate))
                      break; // Don't block on timeout
                    udp_ready_event = &internal_event;
                    internal_event.reset();
                    internal_event.wait(mutex);
                    continue;
                  }
                if (msg_len < target_msg_len)
                  continue; // Not a compatible connection message
                if ((msg[0] != (char) 0xFF) || (msg[1] != (char) 0xFF) ||
                    (msg[2] != (char)((target_msg_len-4)>>8)) ||
                    (msg[3] != (char)((target_msg_len-4)&0xFF)))
                  continue; // Does not have correct 4-byte header
                const char *cp = channel_id;
                for (msg+=4; *cp != '\0'; cp++, msg++)
                  if (*cp != *msg)
                    break;
                if (!udp_channel->connect(peer_addr,this))
                  { // Something went wrong
                    close_channel();
                    break;
                  }
              }
              catch (kdu_exception) { // Force premature closure of the channel
                close_channel();
                break;
              }
            }
        }
    }
  mutex.unlock();
  return result;
}

/*****************************************************************************/
/*                   kds_jpip_channel::get_suggested_bytes                   */
/*****************************************************************************/

int kds_jpip_channel::get_suggested_bytes(int *suggested_chunk_size,
                                          bool blocking,
                                          kdu_event *event_to_signal)
{
  int result = 0;
  mutex.lock();
  if (channel != NULL)
    {
      queue_ready_event = NULL; // Cancel any existing `queue_ready_event'
      while (!result)
        {
          if (holding_bytes < suggested_epoch_bytes)
            { 
              result = suggested_epoch_bytes;
              if (suggested_chunk_size != NULL)
                *suggested_chunk_size = suggested_chunk_bytes;
            }
          else
            { 
              if (((queue_ready_event=event_to_signal) != NULL) || !blocking)
                break; // Return with 0, so caller can wait on event
              if ((timeout_gate < 0) || (last_cur_time < timeout_gate))
                break; // Don't block on timeout
              queue_ready_event = &internal_event;
              internal_event.reset();
              internal_event.wait(mutex);
            }
        }
    }
  mutex.unlock();
  return result;
}

/*****************************************************************************/
/*                     kds_jpip_channel::abandon_chunks                      */
/*****************************************************************************/

void kds_jpip_channel::abandon_chunks(kds_chunk_gap *chunks)
{
  if (udp_channel == NULL)
    return;
  mutex.lock();
  
  kds_chunk_gap *scan;
  kd_queue_elt *qp, *prev_qp, *next_qp;

  // Check the holding queue first.  Note that it is possible that the client
  // abandons chunks which have not yet been transmitted even once.  However,
  // we will not remove any element other than an EOR-only element that
  // indicates pre-emption of the request, until it has had a chance to be
  // transmitted at least once, since the client may abandon data chunks
  // recklessly and it takes quite some effort for the server to generate them.
  for (prev_qp=NULL, qp=holding_head; qp != NULL; prev_qp=qp, qp=next_qp)
    { 
      next_qp = qp->next;
      if (qp->chunk_data == NULL)
        continue; // Should not really happen
      for (scan=chunks; scan != NULL; scan=scan->next)
        if (scan->qid16 == (kdu_uint16) qp->qid_val)
          { 
            int seq_num = qp->chunk_data[5];
            seq_num = (seq_num << 8) + qp->chunk_data[6];
            seq_num = (seq_num << 8) + qp->chunk_data[7];
            if ((seq_num >= scan->seq_from) &&
                ((scan->seq_to < 0) || (seq_num <= scan->seq_to)))
              break;
          }
      if (scan == NULL)
        continue; // Nothing to abandon here
      
      total_client_abandoned_bytes += qp->chunk_bytes;
      
      bool eor_window_change = ((qp->chunk_data == qp->eor_chunk) &&
                                (qp->eor_chunk[9] == JPIP_EOR_WINDOW_CHANGE));
      if ((qp->prev_transmit_time < 0) && !eor_window_change)
        { // Mark as `abandon_requested'
          qp->abandon_requested = true;
          qp->allow_retransmit = false;
          if (first_elt_pending_abandonment == NULL)
            first_elt_pending_abandonment = qp;
        }
      else
        { // Abandon this chunk immediately
          holding_bytes -= qp->chunk_bytes;
          if (prev_qp == NULL)
            holding_head = next_qp;
          else
            prev_qp->next = next_qp;
          if (qp == holding_tail)
            holding_tail = prev_qp;
          if (qp->chunk == NULL)
            return_to_free_list(qp);
          else
            append_to_completed_list(qp);
          qp = prev_qp; // So we don't advance `prev_qp' in the loop
        }
    }
  
  // Now check the transmitted queue.
  for (qp=transmitted_head; qp != NULL; qp=qp->next)
    { 
      if (qp->chunk_data == NULL)
        continue;
      for (scan=chunks; scan != NULL; scan=scan->next)
        if (scan->qid16 == (kdu_uint16) qp->qid_val)
          { 
            int seq_num = qp->chunk_data[5];
            seq_num = (seq_num << 8) + qp->chunk_data[6];
            seq_num = (seq_num << 8) + qp->chunk_data[7];
            if ((seq_num >= scan->seq_from) &&
                ((scan->seq_to < 0) || (seq_num <= scan->seq_to)))
              break;
          }
      if (scan == NULL)
        continue;
      
      total_client_abandoned_bytes += qp->started_bytes;
      
      bool eor_window_change = ((qp->chunk_data == qp->eor_chunk) &&
                                (qp->eor_chunk[9] == JPIP_EOR_WINDOW_CHANGE));
      if (eor_window_change)
        { // Simplest just to mark as no-retransmit
          qp->allow_retransmit = false;
        }
      else
        { // Mark as `abandon_requested'
          qp->abandon_requested = true;
          qp->allow_retransmit = false;
          if (first_elt_pending_abandonment == NULL)
            first_elt_pending_abandonment = qp;
        }
    }
  
  mutex.unlock();
}

/*****************************************************************************/
/*                     kds_jpip_channel::set_barrier_qid                      */
/*****************************************************************************/

void kds_jpip_channel::set_barrier_qid(kdu_long qid)
{
  if (qid <= barrier_qid)
    return;
  mutex.lock();
  
  barrier_qid = qid;
  
  kd_queue_elt *qp, *prev_qp, *next_qp;
  
  // Check the holding queue first, for chunks waiting for retransmission
  for (prev_qp=NULL, qp=holding_head; qp != NULL; prev_qp=qp, qp=next_qp)
    { 
      next_qp = qp->next;
      if ((qp->prev_transmit_time < 0) || (qp->qid_val > barrier_qid))
        continue; // Not a retransmission, or after the barrier
      assert(!qp->abandon_requested);
      if (prev_qp == NULL)
        holding_head = next_qp;
      else
        prev_qp->next = next_qp;
      if (qp == holding_tail)
        holding_tail = prev_qp;
      holding_bytes -= qp->chunk_bytes;
      if (qp->chunk == NULL)
        return_to_free_list(qp);
      else
        { 
          qp->chunk->abandoned = false;
          append_to_completed_list(qp);
        }
      qp = prev_qp; // So we don't advance `prev_qp' in the loop    
    }
  
  // Now check the transmitted queue
  bool reduced_window_bytes = false;
  for (prev_qp=NULL, qp=transmitted_head; qp != NULL; prev_qp=qp, qp=next_qp)
    { 
      next_qp = qp->next;
      if (qp->abandon_requested || (qp->qid_val > barrier_qid))
        continue; // Barriers do not apply to chunks that have been abandoned
      window_bytes -= qp->started_bytes;
      reduced_window_bytes = true;
      if (prev_qp == NULL)
        transmitted_head = next_qp;
      else
        prev_qp->next = next_qp;
      if (qp == transmitted_tail)
        transmitted_tail = prev_qp;
      if (qp->chunk == NULL)
        return_to_free_list(qp);
      else
        { 
          qp->chunk->abandoned = false;
          append_to_completed_list(qp);
        }
      qp = prev_qp; // So we don't advance `prev_qp' in the loop
    }
  
  if (reduced_window_bytes && (holding_bytes != 0) &&
      !(waiting_on_send || waiting_on_delivery_gate))
    { 
      /*
      cur_time = timer.get_ellapsed_microseconds();
      send_data();
       */
      channel->queue_conditions(KDCS_CONDITION_IMMEDIATE);
    }
  
  mutex.unlock();
}

/*****************************************************************************/
/*              kds_jpip_channel::check_pending_abandonment                  */
/*****************************************************************************/

kdu_long
  kds_jpip_channel::check_pending_abandonment(kdu_long max_qid,
                                              kdu_event *event_to_signal)
{
  mutex.lock();
  
  abandonment_event = NULL;
  if (first_elt_pending_abandonment == NULL)
    max_qid = -1;
  else
    { 
      if (max_qid < 0)
        { // Find the most recent transmitted chunk pending abandonment.
          // Note: we cannot rely upon the transmitted chunks appearing in
          // order of the QID, since some of them may have been retransmitted.
          const kd_queue_elt *qp;
          for (qp=holding_head; qp != NULL; qp=qp->next)
            if (qp->abandon_requested && (qp->qid_val > max_qid))
              max_qid = qp->qid_val;
          if (max_qid < 0)
            { // Otherwise no need to consider transmitted list, since all
              // abandoned chunks on the holing list must not yet have been
              // transmitted for the first time, so must have qid values
              // at least as large as those of any transmitted chunk.
              for (qp=first_elt_pending_abandonment; qp != NULL; qp=qp->next)
                if (qp->abandon_requested && (qp->qid_val > max_qid))
                  max_qid = qp->qid_val;
            }
        }
      else if (first_elt_pending_abandonment->qid_val > max_qid)
        { // Need to check if there is any chunk pending abandonment whose
          // `qid_val' is <= `max_qid'.  This is still possible because
          // `first_elt_pending_abandonment' is the first in queued order, but
          // not necessarily in qid order.  If there is one, it will have to
          // be on the transmitted list, since all holding list elements
          // that are pending abandonment necessarily have qid values at
          // least as large as any on the transmitted list and they also
          // have qid values that are monotonically increasing, because
          // they have not yet been transmitted even once.  Hence all
          // holding elements pending abandonment have qid values at
          // least as large as that of `first_elt_pending_abandonment'.
          const kd_queue_elt *qp=first_elt_pending_abandonment->next;
          for (; qp != NULL; qp=qp->next)
            if (qp->qid_val <= max_qid)
              break;
          if (qp == NULL)
            max_qid = -1;
        }
    }
  if ((max_qid >= 0) &&
      (event_to_signal != NULL) &&
      ((abandonment_event == NULL) || (min_max_abandon_qid < 0) ||
       (min_max_abandon_qid > max_qid)))
    { 
      min_max_abandon_qid = max_qid;
      abandonment_event = event_to_signal;
    }
  
  mutex.unlock();
  return max_qid;
}

/*****************************************************************************/
/*                      kds_jpip_channel::push_chunks                        */
/*****************************************************************************/

kds_chunk *kds_jpip_channel::push_chunks(kds_chunk *chunks, kdu_long qid_val,
                                         const char *content_type,
                                         bool finished_using_channel)
{
  kds_chunk *returned_chunks=NULL; // List to return
  kds_chunk *chnk, *chnk_next;
  for (chnk=chunks; chnk != NULL; chnk=chnk->next)
    {
      chnk->abandoned = true; // Until proven otherwise
      assert((chnk->num_bytes < (1<<16)) &&
             (chnk->num_bytes <= max_chunk_bytes));
      if (is_auxiliary)
        { 
          if ((qid_val >= 0) && (((kdu_uint16) qid_val) != next_chunk_qid16))
            { 
              next_chunk_qid16 = (kdu_uint16) qid_val;
              next_chunk_seq_val = 0;
            }
          else
            assert((udp_channel==NULL) || (next_chunk_seq_val < (1<<24)));
          assert(chnk->prefix_bytes == 8);
          if (udp_channel != NULL)
            chnk->data[0] = chnk->data[1] = 0; // Zero UDP control word
          else
            { // First word of TCP chunk holds length 
              chnk->data[0] = (kdu_byte)(chnk->num_bytes >> 8);
              chnk->data[1] = (kdu_byte) chnk->num_bytes;
            }
          chnk->data[2] = (kdu_byte)(next_chunk_qid16 >> 8);
          chnk->data[3] = (kdu_byte) next_chunk_qid16;
          chnk->data[4] = 0; // Repeat field starts out as 0
          chnk->data[5] = (kdu_byte)(next_chunk_seq_val >> 16);
          chnk->data[6] = (kdu_byte)(next_chunk_seq_val >> 8);
          chnk->data[7] = (kdu_byte) next_chunk_seq_val;
          next_chunk_seq_val++;
        }
    }
  mutex.lock();
  
  if (channel == NULL)
    {
      mutex.unlock();
      return chunks;
    }

  bool copy_chunks = !is_auxiliary;
  for (chnk=chunks; chnk != NULL; chnk=chnk_next)
    { 
      chnk_next = chnk->next;
      if (chnk->num_bytes <= chnk->prefix_bytes)
        { // Nothing to transmit for this chunk; return this chunk immediately
          chnk->abandoned = false;
          chnk->next = returned_chunks;
          returned_chunks = chnk;
          continue;
        }
      
      kd_queue_elt *qelt = holding_tail;
      if ((qelt == NULL) || (qelt->chunk_bytes > 0) ||
          (qelt->started_bytes != 0))
        qelt = append_to_holding_list(get_new_element(qid_val));
      assert((qelt->reply_body_bytes <= 0) && (qelt->qid_val == qid_val));
           // Otherwise caller forgot to push in reply before data chunk
      if (!copy_chunks)
        { 
          qelt->chunk_data = chnk->data;
          qelt->chunk = chnk;
          qelt->chunk_bytes = chnk->num_bytes;
        }
      else
        { // Copy chunk and return the original `chnk' immediately
          chnk->next = returned_chunks;
          returned_chunks = chnk;
          chnk->abandoned = false;
          if (qelt->chunk_copy == NULL)
            qelt->chunk_copy = new kdu_byte[max_chunk_bytes];
          qelt->chunk_data = qelt->chunk_copy;
          qelt->chunk_bytes = chnk->num_bytes - chnk->prefix_bytes;
          if (qelt->chunk_bytes <= 0)
            qelt->chunk_bytes = 0;
          else
            memcpy(qelt->chunk_copy,chnk->data+chnk->prefix_bytes,
                   (size_t) qelt->chunk_bytes);
          qelt->chunk = NULL;
        }
      assert(qelt->chunk_bytes > 0);
      holding_bytes += qelt->chunk_bytes;
      if (qelt->reply_bytes > 0)
        {
          assert((qelt->reply_body_bytes < 0) && !is_auxiliary);
          qelt->reply.backspace(2); // Backup over paragraph ending empty-line
          qelt->reply << "Transfer-Encoding: chunked\r\n";
          if (content_type != NULL)
            qelt->reply << "Content-Type: " << content_type << "\r\n\r\n";
        }
      qelt->reply_body_bytes = 0; // Because no longer waiting for the chunk
      if (!is_auxiliary)
        {
          qelt->reply.set_hex_mode(true);
          qelt->reply << qelt->chunk_bytes << "\r\n";
          qelt->reply.set_hex_mode(false);
          int new_reply_bytes = qelt->reply.get_remaining_bytes();
          assert(new_reply_bytes >= qelt->reply_bytes);
          holding_bytes += new_reply_bytes - qelt->reply_bytes;
          qelt->reply_bytes = new_reply_bytes;
          need_chunk_trailer = true;
        }
    }
      
  if (!waiting_to_transmit)
    { 
      /*
      cur_time = timer.get_ellapsed_microseconds();
      if (udp_channel != NULL)
        { // Make sure we catch any datagrams that need to be retransmitted
          // and put them onto the holding list ahead of the chunks that
          // have just been pushed in.
          process_acknowledgements();
          handle_unacknowledged_datagrams();
        }
      send_data();
      */
      channel->queue_conditions(KDCS_CONDITION_IMMEDIATE);
    }
  if (is_auxiliary && finished_using_channel)
    this->no_more_data_expected = true;
  
  mutex.unlock();
  return returned_chunks;
}

/*****************************************************************************/
/*                 kds_jpip_channel::terminate_chunked_data                  */
/*****************************************************************************/

void kds_jpip_channel::terminate_chunked_data(kdu_long qid_val,
                                              kdu_byte eor_code)
{
  if (channel == NULL)
    return;
  if (is_auxiliary && (eor_code == 0))
    return;
  
  mutex.lock();
  
  kd_queue_elt *qelt = holding_tail;
  assert((qelt == NULL) || (qelt->chunk_bytes > 0));
  if (is_auxiliary)
    { 
      if ((qid_val >= 0) && (((kdu_uint16) qid_val) != next_chunk_qid16))
        { 
          next_chunk_qid16 = (kdu_uint16) qid_val;
          next_chunk_seq_val = 0;
        }
      else
        assert((udp_channel==NULL) || (next_chunk_seq_val < (1<<24)));
      qelt = append_to_holding_list(get_new_element(qid_val));
      if (udp_channel != NULL)
        qelt->eor_chunk[0] = qelt->eor_chunk[1] = 0; // Zero UDP control word
      else
        { // First word of TCP chunk holds length
          qelt->eor_chunk[0] = 0;  qelt->eor_chunk[1] = 11;
        }
      qelt->eor_chunk[2] = (kdu_byte)(next_chunk_qid16 >> 8);
      qelt->eor_chunk[3] = (kdu_byte) next_chunk_qid16;
      qelt->eor_chunk[4] = 0; // Repeat field starts out as 0
      qelt->eor_chunk[5] = (kdu_byte)(next_chunk_seq_val >> 16);
      qelt->eor_chunk[6] = (kdu_byte)(next_chunk_seq_val >> 8);
      qelt->eor_chunk[7] = (kdu_byte) next_chunk_seq_val;
      qelt->eor_chunk[8] = 0;
      qelt->eor_chunk[9] = eor_code;
      qelt->eor_chunk[10] = 0;
      qelt->chunk_data = qelt->eor_chunk;
      qelt->chunk_bytes = 11;
      holding_bytes += 11;
    }
  else
    {
      assert(need_chunk_trailer);
      qelt = append_to_holding_list(get_new_element(qid_val));
      if (eor_code != 0)
        {
          kdu_byte buf[3] = {0,eor_code,0};
          qelt->reply.set_hex_mode(true);
          qelt->reply << 3 << "\r\n";
          qelt->reply.set_hex_mode(false);
          qelt->reply.write_raw(buf,3);
        }
      qelt->reply << "0\r\n\r\n"; // Write the chunk trailer
      need_chunk_trailer = false;
      qelt->reply_bytes = qelt->reply.get_remaining_bytes();
      holding_bytes += qelt->reply_bytes;
    }
  if (!waiting_to_transmit)
    { 
      /*
      cur_time = timer.get_ellapsed_microseconds();
      if (udp_channel != NULL)
        { // Make sure we catch any datagrams that need to be retransmitted
          // and put them onto the holding list ahead of any EOR chunks that
          // may have just been added.
          process_acknowledgements();
          handle_unacknowledged_datagrams();
        }
      send_data();
      */
      channel->queue_conditions(KDCS_CONDITION_IMMEDIATE);
    }
  
  mutex.unlock();
}

/*****************************************************************************/
/*                        kds_jpip_channel::push_reply                       */
/*****************************************************************************/

void kds_jpip_channel::push_reply(kdcs_message_block &block, int body_bytes,
                                  kdu_long qid_val)
{
  if (is_auxiliary)
    {
      kdu_error e; e << "Attempting to push reply text into an "
      "auxiliary `kds_jpip_channel' object; this is appropriate only for "
      "primary transport channels.";
    }
  if (channel == NULL)
    return;
  mutex.lock();
  try {
    kd_queue_elt *qelt = holding_tail;
    if ((qelt == NULL) || (qelt->chunk_bytes > 0) ||
        (qelt->started_bytes != 0) || (qelt->reply_body_bytes != 0))
      qelt = append_to_holding_list(get_new_element(qid_val));
    /* This check is inappropriate for primary channels, since we may wish to
       aggregate multiple reply paragraphs into a single element; the
       `kd_queue_elt::qid_val' member is not actually used for queue
       elements that belong to primary JPIP channels.
       assert(qelt->qid_val == qid_val);
    */
    qelt->reply_body_bytes = body_bytes;
    if (need_chunk_trailer)
      {
        assert(qelt->reply_bytes == 0);
        need_chunk_trailer = false;
        qelt->reply << "0\r\n\r\n"; // Write the chunk trailer
      }
    qelt->reply.append(block);
    int new_reply_bytes = qelt->reply.get_remaining_bytes();
    holding_bytes += new_reply_bytes - qelt->reply_bytes;
    qelt->reply_bytes = new_reply_bytes;
    if ((qelt->reply_body_bytes >= 0) && !waiting_to_transmit)
      { 
        /*
        cur_time = timer.get_ellapsed_microseconds();
        send_data();
        */
        channel->queue_conditions(KDCS_CONDITION_IMMEDIATE);
      }
  } catch (...) {
    mutex.unlock();
    throw;
  }
  mutex.unlock();
}

/*****************************************************************************/
/*                kds_jpip_channel::retrieve_completed_chunks                */
/*****************************************************************************/

kds_chunk *
  kds_jpip_channel::retrieve_completed_chunks()
{
  if (completed_head == NULL)
    return NULL; // Quick, unguarded check
  kds_chunk *list=NULL, *tail=NULL;
  mutex.lock();
  while (completed_head != NULL)
    {
      kd_queue_elt *qelt = completed_head;
      if (qelt->chunk != NULL)
        { 
          if (tail != NULL)
            tail = tail->next = qelt->chunk;
          else
            list = tail = qelt->chunk;
          tail->next = NULL;
          qelt->chunk = NULL;
        }
      if ((completed_head = qelt->next) == NULL)
        completed_tail = NULL;
      return_to_free_list(qelt);
    }
  mutex.unlock();
  return list;
}

/*****************************************************************************/
/*                     kds_jpip_channel::retrieve_requests                   */
/*****************************************************************************/

bool kds_jpip_channel::retrieve_requests(kd_request_queue *queue,
                                         bool blocking,
                                         kdu_event *event_to_signal)
{
  mutex.lock();
  if (is_auxiliary)
    { 
      mutex.unlock();
      return false;
    }
  bool got_something = false;
  request_ready_event = NULL;
  while (!got_something)
    { 
      got_something = (requests.peek_head() != NULL);
      if (channel == NULL)
        break;
      try {
        while (requests.read_request(tcp_channel))
          got_something = true;
      }
      catch (kdu_exception) { // Force premature closure of the channel
        close_channel();
        break;
      }
      if (((request_ready_event = event_to_signal) != NULL) || !blocking)
        break;
      else if (!got_something)
        { // Block until a request becomes available, or a timeout occurs
          if ((timeout_gate >= 0) && (timeout_gate <= last_cur_time))
            break; // Timeout has already expired; don't blocking
          request_ready_event = &internal_event;
          internal_event.reset();
          internal_event.wait(mutex);
        }
    }
  if (got_something)
    {
      const kd_request *req;
      while ((req=requests.pop_head()) != NULL)
        queue->push_copy(req);
    }
  mutex.unlock();
  return got_something;
}

/*****************************************************************************/
/*                     kds_jpip_channel::return_requests                     */
/*****************************************************************************/

void kds_jpip_channel::return_requests(kd_request_queue *queue)
{
  mutex.lock();
  queue->transfer_state(&requests); // Append internal requests to `queue'
  requests.transfer_state(queue); // Move entire queue to internal `requests'
  if ((requests.peek_head() != NULL) && (request_ready_event != NULL))
    signal_request_ready_event();
  mutex.unlock();
}

/*****************************************************************************/
/*                      kds_jpip_channel::service_channel                    */
/*****************************************************************************/

void kds_jpip_channel::service_channel(kdcs_channel_monitor *monitor,
                                       kdcs_channel *chnl, int cond_flags)
{
  assert(monitor == this->monitor);
  mutex.lock();
  
  last_cur_time = timer.get_ellapsed_microseconds();
  if ((service_run_time < 0) || !(cond_flags & KDCS_CONDITION_CONTINUOUS))
    service_run_time = last_cur_time;
  
  if (this->channel == NULL)
    { // Channel has already been closed
      mutex.unlock();
      return;
    }
  assert(channel == chnl);
  
  if (cond_flags & KDCS_CONDITION_MONITOR_CLOSING)
    close_channel();
  
  if (cond_flags & KDCS_CONDITION_WAKEUP)
    earliest_wakeup = latest_wakeup = -1; // So we know the channel monitor
                  // needs to have any wakeup conditions installed again.
    
  bool acknowledged_status_changed = false;
  if (cond_flags & KDCS_CONDITION_READ)
    { // Some reading can be done
      if (udp_ready_event != NULL)
        signal_udp_ready_event();
      else if (is_auxiliary)
        acknowledged_status_changed = process_acknowledgements();
      else
        { // Read requests
          try {
            while ((channel != NULL) &&
                   requests.read_request(tcp_channel));
          }
          catch (kdu_exception) { // Force premature closure of the channel
            close_channel();
          }
          if ((request_ready_event != NULL) &&
              ((channel == NULL) || (requests.peek_head() != NULL)))
            signal_request_ready_event();
        }
    }
  
  if ((udp_channel != NULL) && handle_unacknowledged_datagrams())
    acknowledged_status_changed = true;

  if ((timeout_gate >= 0) && (last_cur_time >= timeout_gate))
    {
      earliest_wakeup = latest_wakeup = -1;
      wake_all_blocked_calls();
    }
  
  if ((holding_head != NULL) &&
      (acknowledged_status_changed ||
       (cond_flags & KDCS_CONDITION_IMMEDIATE) ||
       (waiting_on_send && (cond_flags & KDCS_CONDITION_WRITE)) ||
       (waiting_on_delivery_gate && (cond_flags & KDCS_CONDITION_WAKEUP))))
    send_data(); // Automatically invokes `install_wakeup_conditions'
  else if (cond_flags & KDCS_CONDITION_WAKEUP)
    install_wakeup_conditions(); // Have to install the conditions again
  
  mutex.unlock();
}

/*****************************************************************************/
/*                         kds_jpip_channel::send_data                       */
/*****************************************************************************/

void kds_jpip_channel::send_data()
{
  if (channel == NULL)
    return;
  
  waiting_to_transmit = false; // Until we need to wait
  waiting_on_send = false; // Until we find we cannot send data
  waiting_on_delivery_gate = false; // Until we are held up by the gate
  kd_queue_elt *qp = holding_head;
  if ((idle_start >= 0) && (qp != NULL))
    { // Coming out of an idle period
      idle_usecs += service_run_time - idle_start;
      idle_start = -1;
    }
  
  // Make sure the delivery gate does not slip too far behind, if we have
  // not been able to generate data fast enough to keep up with the channel.
  if ((delivery_gate + max_delivery_lag_usecs) < service_run_time)
    delivery_gate = service_run_time - max_delivery_lag_usecs;
  
  int group_elements=0; // Total number of data chunks transmitted together
  int non_initial_group_bytes=0; // Total bytes in all but the first chunk in
                                 // a group, when `group_based_flow_ctl' true.
  int preferred_non_initial_group_bytes = 1;
  if (group_based_flow_ctl)
    { 
      preferred_non_initial_group_bytes += (int)
        (0.5F*((float)max_delivery_lag_usecs)/cur_throttle_usecs_per_byte);
      if (preferred_non_initial_group_bytes > 8192)
        preferred_non_initial_group_bytes = 8192; // 8kB should be enough to
                                                  // get good rate estimates
    }
  
  int new_chunk_bytes = 0; // Number of new bytes transmitted here  
  bool window_was_empty = (transmitted_head == NULL);
  bool scheduled_immediate_return = false;
  while (((qp = holding_head) != NULL) && (qp->reply_body_bytes >= 0))
    { 
      if (group_based_flow_ctl)
        { // Take grouping into account when deciding whether to finish up
          if ((group_elements == 0) ||
              ((group_elements > 1) &&
               ((non_initial_group_bytes+qp->chunk_bytes) >=
                preferred_non_initial_group_bytes)))
            { // Terminate the transmission of available data chunks only if
              // we have not yet sent anything, or if we have sent at least
              // a pair of data chunks such that the cumulative size of the
              // non-initial data chunks would exceed our preferred limit.
              // This policy encourages the transmission of data chunks in
              // groups of a reasonable size, ignoring the `delivery_gate' and
              // `window_threshold' constraints until such groups have been
              // formed.  This is important for the reliable estimation of
              // channel throughput via the `cum_delta_t' and `cum_delta_b'
              // state variables.
              if (service_run_time < delivery_gate)
                { waiting_on_delivery_gate = waiting_to_transmit = true; break; }
              if (window_bytes > window_threshold)
                { waiting_to_transmit = true; break; }
            }
        }
      else
        { // Finish up if we would cross the delivery gate or window threshold
          if (service_run_time < delivery_gate)
            { waiting_on_delivery_gate = waiting_to_transmit = true; break; }
          if (window_bytes > window_threshold)
            { waiting_to_transmit = true; break; }
          if (new_chunk_bytes > 256)
            { // We have already transmitted a modest amount of data; give any
              // other channels with immediate data to send an opportunity to
              // interleave this data with ours.
              channel->queue_conditions(KDCS_CONDITION_IMMEDIATE);
              scheduled_immediate_return = true;
              break;
            }
        }
      
      bool first_attempt = (qp->started_bytes == 0);
      if (first_attempt)
        { 
          qp->started_bytes = qp->reply_bytes + qp->chunk_bytes;
          if (qp->reply_bytes > 0)
            {
              assert(!is_auxiliary);
              kd_msg_log.print((char *)(qp->reply.peek_block()),
                               qp->reply.get_remaining_bytes() -
                               qp->reply_body_bytes,"\t>> ");
            }
        }
      
      if (qp->reply_bytes > 0)
        {
          try {
            if (!tcp_channel->write_block(qp->reply))
              {
                waiting_to_transmit = waiting_on_send = true;
                if (first_attempt && (qp->holdup_ref_time < 0))
                  { // Implement mechanism for estimating HTTP channel rate
                    qp->holdup_ref_time = service_run_time;
                  }
                break;
              }
          }
          catch (kdu_exception) {
            close_channel();
            return;
          }
          qp->reply.restart();
          holding_bytes -= qp->reply_bytes;
          qp->reply_bytes = 0;
        }
      
      if (qp->chunk_bytes > 0)
        { // Send data chunk
          try {
            assert((qp->chunk_data != NULL) &&
                   (qp->chunk_bytes <= max_chunk_bytes));
            if (tcp_channel != NULL)
              { 
                if (!tcp_channel->write_raw(qp->chunk_data,qp->chunk_bytes))
                  { 
                    waiting_to_transmit = waiting_on_send = true;
                    if (first_attempt && (!is_auxiliary) &&
                        (qp->holdup_ref_time < 0))
                      { // Implement mechanism for estimating HTTP channel rate
                        qp->holdup_ref_time = service_run_time;
                      }
                    break;
                  }
              }
            else if (udp_channel != NULL)
              { 
                if (!udp_channel->send_msg(qp->chunk_data,qp->chunk_bytes))
                  { 
                    waiting_to_transmit = waiting_on_send = true;
                    break;
                  }
              }
            else
              assert(0);
          }
          catch (kdu_exception) {
            close_channel();
            return;
          }
          holding_bytes -= qp->chunk_bytes;
          qp->chunk_bytes = 0;
          if (replenishing_timeout > 0)
            timeout_gate = last_cur_time + replenishing_timeout;
          if (!is_auxiliary)
            {
              qp->reply << "\r\n"; // Terminate chunk with CRLF
              qp->reply_bytes = qp->reply.get_remaining_bytes();
              qp->started_bytes += qp->reply_bytes;
              holding_bytes += qp->reply_bytes;
              continue; // Causes us to go back and transmit chunk terminator
            }
        }
      
      // If we get here, the entire queue element has been transmitted
      total_transmitted_bytes += qp->started_bytes;
      if ((holding_head = qp->next) == NULL)
        {
          holding_tail = NULL;
          assert(holding_bytes == 0);
        }
      
      if (!is_auxiliary)
        return_to_free_list(qp);
      else if ((barrier_qid >= 0) && (qp->qid_val <= barrier_qid) &&
               !qp->abandon_requested)
        { // Data chunk implicitly assumed to have arrived -- this is unusual;
          // we would normally only expect the barrier to be applied after the
          // data chunk was first transmitted, but it is legal for a client to
          // declare that everything is unequivocally to be considered
          // successful somehow.
          assert(qp != first_elt_pending_abandonment);
          if (qp->chunk == NULL)
            return_to_free_list(qp);
          else
            { 
              qp->chunk->abandoned = false;
              append_to_completed_list(qp);
            }
        }
      else
        { 
          append_to_transmitted_list(qp);
          window_bytes += qp->started_bytes;
          qp->transmit_time = service_run_time;
          qp->window_bytes = window_bytes;
        }
      
      // Determine the next delivery gate
      if (group_based_flow_ctl)
        { 
          group_elements++;
          if (group_elements > 1)
            non_initial_group_bytes += qp->started_bytes;
        }
      new_chunk_bytes += qp->started_bytes;
      qp->estimated_transport_usecs = 1 +
        (kdu_long)(qp->started_bytes * cur_throttle_usecs_per_byte);
      delivery_gate += qp->estimated_transport_usecs;
      
      if (!is_auxiliary)
        {
          // See if HTTP-only transport channel is idle
          if (holding_head == NULL)
            {
              idle_start = service_run_time;
              if (idle_ready_event != NULL)
                signal_idle_ready_event();
            }
        }

      // See if we need to wake up a blocked call to `get_suggested_bytes'
      if ((queue_ready_event != NULL) &&
          (holding_bytes < suggested_epoch_bytes))
        signal_queue_ready_event();
    }

  if (window_was_empty && (transmitted_head != NULL))
    { // We need to register for reads with the channel monitor, so that
      // we can be sure to receive acknowledgements via calls to the
      // `service_channel' function.
      if (!channel->queue_conditions(KDCS_CONDITION_READ))
        { 
          close_channel();
          return;
        }
    }

  if (!scheduled_immediate_return)
    { 
      service_run_time = -1;
      install_wakeup_conditions();
    }
}

/*****************************************************************************/
/*                 kds_jpip_channel::install_wakeup_conditions               */
/*****************************************************************************/

void kds_jpip_channel::install_wakeup_conditions()
{
  if (channel == NULL)
    return;

  kdu_long early_gate=timeout_gate, late_gate=timeout_gate;
  if ((holding_head == NULL) || (holding_head->reply_body_bytes < 0))
    waiting_to_transmit = waiting_on_send = waiting_on_delivery_gate = false;
      // Just in case the holding queue was cleared somehow without a call
      // to `send_data' making it happen -- may have happened when processing
      // acknowledgements, for example.

  if (waiting_on_delivery_gate &&
      ((early_gate < 0) || (delivery_gate < early_gate)))
    { 
      kdu_long slack = 1000; // At least 1ms slack
      slack += // At least 1 chunk of slack also included
        (kdu_long)(cur_throttle_usecs_per_byte * max_chunk_bytes);
      if (slack > (max_delivery_lag_usecs>>2))
        slack = max_delivery_lag_usecs>>2; // Avoid letting the throttle slip
      early_gate = delivery_gate;
      late_gate = early_gate + slack;
    }
  
  if (udp_channel != NULL)
    { // Look for pending acknowledgement timeouts
      kd_queue_elt *qp;
      for (qp=transmitted_head; qp != NULL; qp=qp->next)
        { 
          kdu_long max_delay = max_ack_offset +
            estimated_network_delay_usecs*KD_UDP_RETRANSMIT_FACTOR;
          if (max_delay > max_intrinsic_ack_delay)
            max_delay = max_intrinsic_ack_delay;
          max_delay += 1 + (kdu_long)
            (qp->window_bytes * estimated_network_usecs_per_byte *
             KD_UDP_RETRANSMIT_FACTOR);
          kdu_long ack_min = qp->transmit_time + max_delay;
          kdu_long ack_max = ack_min + (max_delay>>1);
          if (early_gate < 0)
            { early_gate = ack_min; late_gate = ack_max; }
          else
            { 
              if (ack_min < early_gate)
                early_gate = ack_min;
              if (ack_max < late_gate)
                late_gate = ack_max;
            }
        }
    }
  if ((early_gate != earliest_wakeup) || (late_gate != latest_wakeup))
    { 
      earliest_wakeup = early_gate;
      latest_wakeup = late_gate;
      channel->schedule_wakeup(early_gate,late_gate);
    }
}

/*****************************************************************************/
/*                 kds_jpip_channel::process_acknowledgements                */
/*****************************************************************************/

bool kds_jpip_channel::process_acknowledgements()
{
  if ((transmitted_head == NULL) && (udp_channel == NULL))
    return false; // Note: for UDP transports, we need to always be on the
                  // lookout for acknowledgement messages, even if there are
                  // no outstanding transmitted datagrams, because the
                  // channel is unreliable, so datagrams may be multiply
                  // acknowledged.  Also, and perhaps most importantly,
                  // the transmitted list might have been recently emptied
                  // by `handle_unacknowledged_datagrams', yet we want to
                  // be able to detect late arriving acknowledgements for
                  // such datagrams, which are likely to be sitting on the
                  // holding queue, awaiting retransmission.
  
  // Parameters used to update the congestion control policy
  int num_reliable_acks = 0; // Number of acknowlegements we can use for policy
  kdu_long cum_reliable_ack_bytes = 0; // Reliably acknowledged chunk lengths
  kdu_long cum_reliable_ack_rtt_usecs = 0; // Reliably acknowledged RTT's
  kdu_long cum_reliable_ack_windows = 0; // Window sizes when data was sent
  
  // Parameters used to update the UDP acknowledgement window
  int num_rtts = 0; // Num acknowledgements in this call with RTT's we can use
  kdu_long max_rtt_usecs = 0; // Max usable RTT found in this call
  kdu_long cum_rtt_windows = 0; // Sum of window sizes for all `num_rtts'
  
  // Parameters used for the group-based flow control algorithm
  int best_group_ack_bytes=0;
  kdu_long best_group_ack_rtt=1;
  bool need_estimated_group_rate_update = false;
  bool have_estimated_group_delay_update = false;
  
  bool removed_transmitted_element = false;  
  int ack_bytes = 0;
  kdu_byte *ack = NULL;
  for (; (channel != NULL) &&
       ((transmitted_head != NULL) || (udp_channel != NULL));
       ack+=8, ack_bytes-=8)
    { 
      if (ack_bytes < 8)
        { 
          try {
            if (tcp_channel != NULL)
              { 
                if ((ack = tcp_channel->read_raw(8)) == NULL)
                  break;
                ack_bytes = 8;
              }
            else if (udp_channel != NULL)
              { 
                ack = udp_channel->recv_msg(ack_bytes,512);
                if (ack == NULL)
                  break;
                if ((ack_bytes & 7) || (ack_bytes < 8) ||
                    ((ack[0] == 0xFF) && (ack[1] = 0xFF)))
                  continue; // Probably a duplicate establishment datagram
              }
            else
              assert(0);
          }
          catch (kdu_exception) {
            close_channel();
            break;
          }
        }
      
      // If we get here, we have received an acknowledgement message.
      kd_queue_elt *qp_prev=NULL, *qp=transmitted_head;
      kdu_long client_ack_delay = 0;
      bool update_window = true; // -> false if not regular acknowledgement
      if (udp_channel != NULL)
        { 
          client_ack_delay = 0;
          if (ack[1] > 0)
            { // Delay is 2^{D/8} microseconds, where D = `ack[1]'
              int exp = ack[1] >> 3;
              if (exp > 22)
                exp = 22; // Ridiculously large!
              client_ack_delay = kds_ack_delay_lut_x256[ack[1] & 7];
              client_ack_delay <<= exp;
              client_ack_delay >>= 8;
            }

          for (; qp != NULL; qp_prev=qp, qp=qp->next)
            { 
              if ( // Bytes 0 and 1 hold the control word
                  (ack[2]==qp->chunk_data[2])&&(ack[3]==qp->chunk_data[3]) &&
                  (ack[5]==qp->chunk_data[5])&& // Byte 4 is the repeat field
                  (ack[6]==qp->chunk_data[6])&&(ack[7]==qp->chunk_data[7]))
                break;
            }
          if (qp == NULL)
            { // See if the chunk is waiting for retransmission
              update_window = false;
              bool found_match = false;
              for (qp=holding_head, qp_prev=NULL;
                   (qp != NULL) && (qp->prev_transmit_time >= 0);
                   qp_prev=qp, qp=qp->next)
                if ( // Bytes 0 and 1 hold the control word
                    (ack[2]==qp->chunk_data[2])&&(ack[3]==qp->chunk_data[3]) &&
                    (ack[5]==qp->chunk_data[5])&& // Byte 4 is the repeat field
                    (ack[6]==qp->chunk_data[6])&&(ack[7]==qp->chunk_data[7]))
                  { found_match = true; break;  }
              if (found_match)
                { // Put chunk back on transmitted queue for simplicity
                  if (qp_prev == NULL)
                    holding_head = qp->next;
                  else
                    qp_prev->next = qp->next;
                  if (qp == holding_tail)
                    { assert(qp->next == NULL);
                      holding_tail = qp_prev; }
                  holding_bytes -= qp->chunk_bytes;
                  window_bytes += qp->chunk_bytes;
                  qp->started_bytes = qp->chunk_bytes;  qp->chunk_bytes = 0;
                  qp->transmit_time = qp->prev_transmit_time;
                  qp->prev_transmit_time = -1; // Not known if there was one
                  if (qp->chunk_data[4] > 0)
                    qp->chunk_data[4]--;
                  qp_prev = transmitted_tail;
                  append_to_transmitted_list(qp);
                }
              else
                qp = NULL;
            }
          if (qp == NULL)
            { // Chunk must have been abandoned or acknowledged already
              continue;
            }
        }
            
      if (qp->chunk != NULL)
        qp->chunk->abandoned = false;
      removed_transmitted_element = true;
      total_acknowledged_bytes += qp->started_bytes;
      window_bytes -= qp->started_bytes;
      if (udp_channel != NULL)
        { // Update loss estimating state variables
          dgram_completed_byte_count += qp->started_bytes;
          if (ack[4] != 0)
            { // First received acknowledgement was from a retransmitted
              // datagram; we can assume that earlier transmissions were lost
              dgram_loss_count += ack[4];
              dgram_completed_byte_count += qp->started_bytes*(ack[4]-1);
            }
          normalize_dgram_loss_statistics();
        }

      if (qp_prev == NULL)
        transmitted_head = qp->next;
      else
        qp_prev->next = qp->next;
      if (qp == transmitted_tail)
        { 
          assert(qp->next == NULL);
          transmitted_tail = qp_prev;
        }
      else
        assert(qp->next != NULL);
      
      if (qp == first_elt_pending_abandonment)
        { 
          do { 
            first_elt_pending_abandonment=first_elt_pending_abandonment->next;
          } while ((first_elt_pending_abandonment != NULL) &&
                   !first_elt_pending_abandonment->abandon_requested);
          bool on_holding_list = false;
          if (first_elt_pending_abandonment == NULL)
            { // Check on the holding list
              on_holding_list = true;
              first_elt_pending_abandonment = holding_head;
              while ((first_elt_pending_abandonment != NULL) &&
                     !first_elt_pending_abandonment->abandon_requested)
                first_elt_pending_abandonment =
                  first_elt_pending_abandonment->next;
            }
          if (abandonment_event != NULL)
            { // See if we have passed the `min_max_abandon_qid' point
              const kd_queue_elt *scan = first_elt_pending_abandonment;
              for (; scan != NULL; scan=scan->next)
                if (scan->abandon_requested &&
                    (scan->qid_val <= min_max_abandon_qid))
                  break;
              if ((scan == NULL) && !on_holding_list)
                for (scan=holding_head; scan != NULL; scan=scan->next)
                  if (scan->abandon_requested &&
                      (scan->qid_val <= min_max_abandon_qid))
                    break;
              if (scan == NULL)
                { 
                  signal_abandonment_event();
                  min_max_abandon_qid = -1;
                }
            }
        }

      if (transmitted_head == NULL)
        { 
          assert((transmitted_tail == NULL) && (window_bytes == 0));
          if (holding_head == NULL)
            { // All available data now completely transmitted
              assert(first_elt_pending_abandonment == NULL);
              idle_start = last_cur_time; // Channel is now idle
              if (idle_ready_event != NULL)
                signal_idle_ready_event();
            }
        }
      
      if (qp->chunk == NULL)
        return_to_free_list(qp);
      else
        append_to_completed_list(qp);
      
      kdu_long ack_time = last_cur_time - client_ack_delay;
      kdu_long rtt_usecs = ack_time - qp->transmit_time;
      if (ack[4] != qp->chunk_data[4])
        { // Acknowledgement is from a different transmitted version of the
          // data chunk.  This is actually quite common, since we may observe
          // the acknowledgement from a chunk after deciding to retransmit it.
          update_window = false;
          if (qp->prev_transmit_time < 0)
            continue; // Something has gone wrong here -- never retransmitted
          rtt_usecs = ack_time - qp->prev_transmit_time;
          if (qp->chunk_data[4] == (ack[4]+1))
            { // Received acknowledgement of previous transmission; can
              // reliably infer statistics from this.
              qp->transmit_time = qp->prev_transmit_time;
            }
          else
            ack_time = -1; // Don't use acknowledgement to infer channel stats
        }
      if (rtt_usecs < 100)
        rtt_usecs = 100; // Avoid ridiculously small RTT's on local loopback
      if (group_based_flow_ctl)
        { // Flow control based on `group_cum_delta_...' values
          qp->cum_group_ack_bytes += qp->started_bytes;
          assert(tcp_channel != NULL);
          if ((transmitted_head != NULL) &&
              (qp->transmit_time == transmitted_head->transmit_time))
            { 
              transmitted_head->cum_group_ack_bytes = qp->cum_group_ack_bytes;
              transmitted_head->last_group_ack_time = ack_time;
            }
          if (qp->last_group_ack_time > 0)
            { 
              kdu_long delta_t = ack_time - qp->last_group_ack_time;
              if (delta_t > 0)
                group_cum_delta_t += 0.000001 * (double) delta_t;
              group_cum_delta_b += qp->started_bytes;
              need_estimated_group_rate_update = true;
            }
          else
            { // Acknowledging first element of a group
              normalize_group_cum_delta_t_delta_b();
              
              // Update delay estimate based upon `rtt_usecs'
              kdu_long delay_usecs = (rtt_usecs<10000)?10000:rtt_usecs;
                // Note that we prefer to err on the side of
                // over-estimating the delay; otherwise, in the event
                // of large delay jitter, the window threshold could
                // become overly constrained, throttling throughput.
                // For this reason, we do not subtract
                // `qp->estimated_transport_usecs' from the value of
                // `rtt_usecs' and we lower bound the delay at 10ms,
                // which should be enough to address context switching
                // delays for server/client threads involved in the
                // round trip process.
              if (rtt_usecs < estimated_network_delay_usecs)
                estimated_network_delay_usecs = rtt_usecs;
              else
                { // Allow the delay to gradually rise to `delay_usecs'
                  // with a time constant of about 2 * `delay_usecs'
                  kdu_long msg_usecs = qp->estimated_transport_usecs;
                  kdu_long delta = (delay_usecs -
                                    estimated_network_delay_usecs);
                  delta=(delta*msg_usecs+delay_usecs)/(delay_usecs<<1);
                  estimated_network_delay_usecs += delta;
                }
              have_estimated_group_delay_update = true;
            }

          // See if we need to update the conservative lower bound for the
          // channel throughput which is represented by
          // `best_ack_bytes'/`best_ack_rtt'.
          if ((rtt_usecs > 0) &&
              ((best_group_ack_rtt * qp->cum_group_ack_bytes) >
               (rtt_usecs * best_group_ack_bytes)))
            { 
              best_group_ack_rtt = rtt_usecs;
              best_group_ack_bytes = qp->cum_group_ack_bytes;
            }
        }
      else if ((min_rtt_usecs < 100) || (rtt_usecs < min_rtt_usecs))
        { // Flow control based on `min_rtt_usecs' and `queueing_tgt_bytes'.
          // Update base-RTT (T0) parameter; this may affect Lnom
          min_rtt_usecs = rtt_usecs;
          kdu_long queueing_bytes = min_rtt_usecs / 20; // 5kB per 100ms delay
          if (queueing_bytes < 4000)
            queueing_bytes = 4000;
          else if (queueing_bytes > 12000)
            queueing_bytes = 12000;
          queueing_tgt_bytes = max_chunk_bytes * // Round up to whole # chunks
            (1 + (((int)queueing_bytes-1) / max_chunk_bytes));
        }
      
      total_rtt_usecs += rtt_usecs;
      total_rtt_events++;
      
      if (ack_time > 0)
        { // Accumulate local stats for use in network state estimation (below)
          num_rtts++;
          if (rtt_usecs > max_rtt_usecs)
            max_rtt_usecs = rtt_usecs;
          cum_rtt_windows += qp->window_bytes;
          if (update_window && (qp->started_bytes > 0))
            { // Update stats for congestion control
              num_reliable_acks++;
              cum_reliable_ack_bytes += qp->started_bytes;
              cum_reliable_ack_rtt_usecs += rtt_usecs;
              cum_reliable_ack_windows += qp->window_bytes;
            }
        }

      if ((queue_ready_event != NULL) &&
          (holding_bytes < suggested_epoch_bytes))
        signal_queue_ready_event();
    }

  if (group_based_flow_ctl)
    { // May need to update rate/delay statistics and modify dependent state
      // variables
      if (best_group_ack_bytes > 0)
        { // We have a conservative lower bound for the channel throughput that
          // should be compared with the `estimated_network_rate' to see if we
          // can boost it very quickly.
          assert(best_group_ack_rtt > 0);
          double best_b = best_group_ack_bytes;
          double best_t = 0.000001 * (double) best_group_ack_rtt; // -> seconds
          if ((best_b*group_cum_delta_t) > (group_cum_delta_b*best_t))
            { // Correct things by reducing `cum_group_delta_t'
              group_cum_delta_t = (group_cum_delta_b*best_t)/best_b;
              need_estimated_group_rate_update = true;
            }
        }
      if (need_estimated_group_rate_update)
        estimated_network_rate =
          (float)(group_cum_delta_b / group_cum_delta_t);
      if (need_estimated_group_rate_update ||
          have_estimated_group_delay_update)
        { 
          estimated_network_usecs_per_byte = 1000000.0f/estimated_network_rate;
          max_delivery_lag_usecs = estimated_network_delay_usecs >> 1;
          if (max_delivery_lag_usecs < 5000)
            max_delivery_lag_usecs=5000; // Assumes network can buffer at least
                                         // 5ms of data -- else retransmissions
                                         // may increase.
          float min_r = estimated_network_rate;
          min_r = (min_r < max_bytes_per_second)?min_r:max_bytes_per_second;
          float threshold = max_bytes_per_second * 2.0F;
          if (estimated_network_rate < threshold)
            threshold = estimated_network_rate;
          threshold *= 0.00000075F; // min{0.75x actual rate, 1.5x max rate}
          threshold *= estimated_network_delay_usecs;
          window_threshold = 1 + (int) threshold;

          cur_throttle_usecs_per_byte = estimated_network_usecs_per_byte;
          float min_throttle_usecs_per_byte = 1000000.0f/max_bytes_per_second;
          if (cur_throttle_usecs_per_byte < min_throttle_usecs_per_byte)
            cur_throttle_usecs_per_byte = min_throttle_usecs_per_byte;

          calculate_suggested_epoch_and_chunk_sizes(total_rtt_events < 1);
        }
    }
  else
    { // Using Vegas-like congestion management algorithm
      float alpha=0.0f; // Update factor to use for recursive updates
      if (num_reliable_acks > 0)
        { // Implement congestion window update algorithm
          float Lnom = (float)queueing_tgt_bytes;
          float T0 = (float)min_rtt_usecs;
          float cumT = (float)cum_reliable_ack_rtt_usecs;
          float T0_over_cumT = T0 / cumT;
      
          // Prevent T0/T becoming larger than 15/16.  Since at equilibrium the
          // W = Lnom / [1-T/T0], ensuring that W does not grow beyond 16*Lnom.
          float max_T0_over_cumT = (15.0f/16.0f)*num_reliable_acks;
          if (T0_over_cumT > max_T0_over_cumT)
            T0_over_cumT = max_T0_over_cumT;
      
          // Calculate target window size based on the N (`num_reliable_acks')
          // acknowledgements whose window sizes and RTT's we are using.  The
          // formula is equivalent to Wbar*T0/Tbar + Lnom, where Wbar and Tbar
          // are the average values of the W_q and T_q values for each of the N
          // acknowledged elements q that we are using.
          float cumW = (float)cum_reliable_ack_windows;
          float win_tgt = Lnom + T0_over_cumT * cumW;
#ifdef KD_PRINT_CHANNEL_STATE_UPDATES
          printf("    W=%g; T=%gms; Win-tgt=%g\n",
                 cumW/num_reliable_acks,0.001f*cumT/num_reliable_acks,win_tgt);
#endif
      
          // Calculate the update factor alpha, which is roughly interpreted
          // as the fraction of a full RTT represented by the data that has
          // been reliably acknowledged here.
          float win_thresh = (float)window_threshold;
          alpha = ((float)cum_reliable_ack_bytes / win_thresh);
          alpha = (alpha > 0.75f)?0.75f:0.75f;
      
          // Update the window threshold using `win_tgt' and `alpha'
          win_thresh += alpha * (win_tgt - win_thresh);
          if (win_thresh < Lnom)
            win_thresh = Lnom;
          window_threshold = 1 + (int)win_thresh;

          // Update delay estimates using average observed delay and `alpha'
          float avg_delay = cumT;
          if (num_reliable_acks > 1)
            avg_delay *= (1.0f / (float)num_reliable_acks);
          if (avg_delay < 1000.0f)
            avg_delay = 1000.0f; // Don't estimate delays smaller than 1ms
          float cur_delay = (float)estimated_network_delay_usecs;
          cur_delay += alpha*(avg_delay - cur_delay);
          estimated_network_delay_usecs = 1 + (kdu_long)cur_delay;
      
          // Update channel rate estimates
          estimated_network_usecs_per_byte = cur_delay / win_thresh;
          estimated_network_rate = 1000000.0f/estimated_network_usecs_per_byte;
          cur_throttle_usecs_per_byte = 0.5f*estimated_network_usecs_per_byte;
          float min_throttle_usecs_per_byte = 1000000.0f/max_bytes_per_second;
          if (cur_throttle_usecs_per_byte < min_throttle_usecs_per_byte)
            cur_throttle_usecs_per_byte = min_throttle_usecs_per_byte;

          max_delivery_lag_usecs = estimated_network_delay_usecs >> 1;
          if (max_delivery_lag_usecs < 10000)
            max_delivery_lag_usecs = 10000; // Allow for system wake precision
          calculate_suggested_epoch_and_chunk_sizes(total_rtt_events < 1);
#ifdef KD_PRINT_CHANNEL_STATE_UPDATES
          printf("W_t = %d; Lnom = %d; T0 = %gms; T_est = %gms; "
                 "R_est = %gkB/s; Throttle = %gkB/s\n",
                 window_threshold,queueing_tgt_bytes,0.001f*(float)min_rtt_usecs,
                 0.001f*(float)estimated_network_delay_usecs,
                 0.001f*estimated_network_rate,
                 1000.0f/cur_throttle_usecs_per_byte);
#endif
        }

      if ((udp_channel != NULL) && (num_rtts > 0))
        { // Figure out whether or not we need to adjust the `max_ack_offset'
          // value based upon the observed round-trip-times.
          float avg_win = (float)cum_rtt_windows;
          if (num_rtts > 1)
            avg_win *= (1.0f / num_rtts);
          float tgt_ack_offset = (1.5f * (float)max_rtt_usecs) -
          KD_UDP_RETRANSMIT_FACTOR*(((float)estimated_network_delay_usecs) +
                                    avg_win*estimated_network_usecs_per_byte);
          if (tgt_ack_offset < 10000.0f)
            tgt_ack_offset = 10000.0f; // Always allow at least 10ms to account
                                       // OS context switching granularity.
          float cur_ack_offset = (float)max_ack_offset;
          if (tgt_ack_offset > cur_ack_offset)
            { // In this case, A(q) > A, so we just assign A(q) to A
              max_ack_offset = 1 + (kdu_long)tgt_ack_offset;
            }
          else if (alpha > 0.0f)
            { // We want to update A to A - alpha*(A-A(q)), where alpha is the
              // parameter found above that represents the ratio between the
              // total amount of data found in reliably acknowledged queue
              // elements relative to the prevailing window threshold.  If
              // alpha is zero, we did not find any reliably acknowledged queue
              // elements with data, so we do not adjust `max_ack_offset'
              // downwards here.
              assert(alpha <= 1.0f);
              cur_ack_offset += alpha*(tgt_ack_offset - cur_ack_offset);
              max_ack_offset = 1 + (kdu_long)cur_ack_offset;
            }
        }
    }
  
  return removed_transmitted_element;
}

/*****************************************************************************/
/*             kds_jpip_channel::handle_unacknowledged_datagrams             */
/*****************************************************************************/

bool kds_jpip_channel::handle_unacknowledged_datagrams()
{
  if ((udp_channel == NULL) || (transmitted_head == NULL))
    return false;
  kdu_long partial_max_delay = max_ack_offset +
    estimated_network_delay_usecs*KD_UDP_RETRANSMIT_FACTOR;
  
  bool removed_something = false;
  kd_queue_elt *qp_prev=NULL, *qp, *qp_next;
  for (qp=transmitted_head; qp != NULL; qp_prev=qp, qp=qp_next)
    { 
      qp_next = qp->next;
      kdu_long max_delay = partial_max_delay;
      if (max_delay > max_intrinsic_ack_delay)
        max_delay = max_intrinsic_ack_delay;
      max_delay += (kdu_long)
        (qp->window_bytes * estimated_network_usecs_per_byte *
         KD_UDP_RETRANSMIT_FACTOR);
      if ((last_cur_time - qp->transmit_time) < max_delay)
        continue;
      
      // Datagram is overdue -- if this is already a retransmission, the
      // qp->last_max_ack_offset value will be non-zero, so we can lower
      // bound `max_ack_offset' by twice the value that prevailed during
      // the previous retransmission generation.
      kdu_long bound = qp->last_max_ack_offset << 1;
      qp->last_max_ack_offset = max_ack_offset;
      if (bound > max_ack_offset)
        max_ack_offset =
          (bound > max_intrinsic_ack_delay)?max_intrinsic_ack_delay:bound;

      removed_something = true;
      
      // Next, unlink `qp' from the transmitted list
      assert((qp->chunk_bytes == 0) && (qp->started_bytes > 0));
      if (qp_prev == NULL)
        transmitted_head = qp_next;
      else
        qp_prev->next = qp_next;
      if (qp == transmitted_tail)
        transmitted_tail = qp_prev;
      window_bytes -= qp->started_bytes;
      
      bool retransmit_allowed =
        (this->allow_retransmit &&
         (qp->chunk_data[4] < KD_UDP_MAX_RETRANSMISSIONS));
      
      if (qp->abandon_requested || !retransmit_allowed)
        { // `qp' needs to be abandoned, not retransmitted
          if (!retransmit_allowed)
            { // Since we are not allowed to retransmit datagrams in this mode,
              // we have to assume that this datagram has been lost, in order
              // to estimate loss statistics
              dgram_completed_byte_count += qp->started_bytes;
              dgram_loss_count++;
              normalize_dgram_loss_statistics();
              qp->allow_retransmit = false;
            }
            
          if (qp == first_elt_pending_abandonment)
            { 
              first_elt_pending_abandonment = qp_next;
              while ((first_elt_pending_abandonment != NULL) &&
                     !first_elt_pending_abandonment->abandon_requested)
                first_elt_pending_abandonment =
                  first_elt_pending_abandonment->next;
              bool on_holding_list = false;
              if (first_elt_pending_abandonment == NULL)
                { // Check on the holding list
                  on_holding_list = true;
                  first_elt_pending_abandonment = holding_head;
                  while ((first_elt_pending_abandonment != NULL) &&
                         !first_elt_pending_abandonment->abandon_requested)
                    first_elt_pending_abandonment =
                      first_elt_pending_abandonment->next;
                }
              if (abandonment_event != NULL)
                { // See if we have passed the `min_max_abandon_qid' point
                  const kd_queue_elt *scan = first_elt_pending_abandonment;
                  for (; scan != NULL; scan=scan->next)
                    if (scan->abandon_requested &&
                        (scan->qid_val <= min_max_abandon_qid))
                      break;
                  if ((scan == NULL) && !on_holding_list)
                    for (scan=holding_head; scan != NULL; scan=scan->next)
                      if (scan->abandon_requested &&
                          (scan->qid_val <= min_max_abandon_qid))
                        break;                  
                  if (scan == NULL)
                    { 
                      signal_abandonment_event();
                      min_max_abandon_qid = -1;
                    }
                }
            }
          assert(!qp->allow_retransmit);
        }
      
      if (!qp->allow_retransmit)
        append_to_completed_list(qp);
      else
        { // Move `qp' to the head of the holding queue
          assert(qp != first_elt_pending_abandonment);
          qp->chunk_bytes = qp->started_bytes;  qp->started_bytes = 0;
          holding_bytes += qp->chunk_bytes;
          qp->prev_transmit_time = qp->transmit_time;  qp->transmit_time = -1;  
          qp->chunk_data[4]++; // Increase "repeat" count in chunk header
          reinsert_into_holding_list(qp);
        }
      
      qp = qp_prev; // So `qp_prev' does not change as the loop iterates
    }
  
  return removed_something;
}

/*****************************************************************************/
/*        kds_jpip_channel::calculate_suggested_epoch_and_chunk_sizes        */
/*****************************************************************************/

void kds_jpip_channel::calculate_suggested_epoch_and_chunk_sizes(bool startup)
{
  if (!is_auxiliary)
    { // Flow control is managed by the client so allow ourselves to generate
      // data in large (and hence efficient) epochs, subject to the client
      // imposed length constraint.
      suggested_chunk_bytes = max_chunk_bytes;
      suggested_epoch_bytes = 256000; // Prepare up to 256kB at a time
    }
  else if (startup)
    { 
      suggested_chunk_bytes = min_suggested_chunk_bytes;
      suggested_epoch_bytes = 3*suggested_chunk_bytes;
    }
  else
    { 
      float min_r = estimated_network_rate;
      min_r = (min_r < max_bytes_per_second)?min_r:max_bytes_per_second;
      suggested_epoch_bytes = 1 + (int)
        (KDCS_TARGET_SERVICE_EPOCH_SECONDS*min_r);
      suggested_chunk_bytes = 8 + (suggested_epoch_bytes >> 3);
      if ((udp_channel != NULL) && (dgram_loss_count > 0))
        { // Account for losses
          int optimal_size = -dgram_overhead + (int)
            sqrt(((double)(dgram_overhead+8)) *
                 (dgram_completed_byte_count/dgram_loss_count));
          if (suggested_chunk_bytes > optimal_size)
            suggested_chunk_bytes = optimal_size;
        }
      if (suggested_chunk_bytes > max_chunk_bytes)
        suggested_chunk_bytes = max_chunk_bytes;
      else if (suggested_chunk_bytes < min_suggested_chunk_bytes)
        suggested_chunk_bytes = min_suggested_chunk_bytes;
      if (suggested_epoch_bytes < (2*suggested_chunk_bytes-16))
        suggested_epoch_bytes = 2*suggested_chunk_bytes-16;
    }
}

/*****************************************************************************/
/*                       kds_jpip_channel::close_channel                     */
/*****************************************************************************/

void kds_jpip_channel::close_channel()
{
  if (channel != NULL)
    { 
      channel->close();
      delete channel;
      channel = NULL;
      tcp_channel = NULL;
      udp_channel = NULL;
    }
  first_elt_pending_abandonment = NULL;
  min_max_abandon_qid = -1;
  barrier_qid = -1;
  while ((transmitted_tail = transmitted_head) != NULL)
    {
      transmitted_head = transmitted_tail->next;
      if (transmitted_tail->chunk != NULL)
        append_to_completed_list(transmitted_tail);
      else
        return_to_free_list(transmitted_tail);
    }
  while ((holding_tail = holding_head) != NULL)
    {
      holding_head = holding_tail->next;
      if (holding_tail->chunk != NULL)
        append_to_completed_list(holding_tail);
      else
        return_to_free_list(holding_tail);
    }
  holding_bytes = window_bytes = 0;
  wake_all_blocked_calls();
  if (idle_start < 0)
    idle_start = last_cur_time;
}


/* ========================================================================= */
/*                          kd_target_description                            */
/* ========================================================================= */

/*****************************************************************************/
/*                  kd_target_description::parse_byte_range                  */
/*****************************************************************************/

bool
  kd_target_description::parse_byte_range()
{
  range_start = 0;
  range_lim = KDU_LONG_MAX;
  const char *cp=byte_range;
  while (*cp == ' ') cp++;
  if (*cp == '\0')
    return true;
  
  while ((*cp >= '0') && (*cp <= '9'))
    range_start = (range_start*10) + (*(cp++) - '0');
  if (*(cp++) != '-')
    { range_start=0; return false; }
  range_lim = 0;
  while ((*cp >= '0') && (*cp <= '9'))
    range_lim = (range_lim*10) + (*(cp++) - '0');
  if ((range_lim <= range_start) || ((*cp != '\0') && (*cp != ' ')))
    { range_lim = KDU_LONG_MAX; return false; }
  range_lim++; // Since the upper end of the supplied range string is inclusive
  return true;
}


/* ========================================================================= */
/*                            kd_connection_thread                           */
/* ========================================================================= */

/*****************************************************************************/
/*                        connection_thread_start_proc                       */
/*****************************************************************************/

static kdu_thread_startproc_result KDU_THREAD_STARTPROC_CALL_CONVENTION
  connection_thread_start_proc(void *param)
{
  kd_connection_thread *obj = (kd_connection_thread *) param;
  obj->thread_entry();
  return KDU_THREAD_STARTPROC_ZERO_RESULT;
}

/*****************************************************************************/
/*                kd_connection_thread::kd_connection_thread                 */
/*****************************************************************************/

kd_connection_thread::kd_connection_thread(kd_connection_server *owner,
                                           kdcs_channel_monitor *monitor,
                                           kd_delegator *delegator,
                                           kd_source_manager *source_manager,
                                           int min_suggested_chunk_size,
                                           int max_chunk_size,
                                           float max_bytes_per_second,
                                           float max_establishment_seconds,
                                           bool restrict_access)
{
  this->owner = owner;
  this->monitor = monitor;
  this->delegator = delegator;
  this->source_manager = source_manager;
  this->min_suggested_chunk_bytes = min_suggested_chunk_size;
  this->max_chunk_bytes = max_chunk_size;
  this->max_bytes_per_second = max_bytes_per_second;
  this->max_establishment_seconds = max_establishment_seconds;
  this->restrict_access = restrict_access;
  next = NULL;
}

/*****************************************************************************/
/*                        kd_connection_thread::start                        */
/*****************************************************************************/

bool kd_connection_thread::start()
{
  return thread.create(connection_thread_start_proc,this);
}

/*****************************************************************************/
/*                     kd_connection_thread::thread_entry                    */
/*****************************************************************************/

void kd_connection_thread::thread_entry()
{
  owner->mutex.lock();
  while (!owner->finish_requested)
    {
      kds_jpip_channel *jpip_channel = owner->returned_head;
      if (jpip_channel != NULL)
        {
          if ((owner->returned_head = jpip_channel->next) == NULL)
            owner->returned_tail = NULL;
          jpip_channel->next = NULL;
        }
      else
        {
          kdcs_tcp_channel *tcp_channel = NULL;
          try {
            if (!owner->shutdown_in_progress)
              tcp_channel = owner->listener->accept(monitor,NULL,true);   
          }
          catch (kdu_exception exc) {
            if (exc == KDU_ERROR_EXCEPTION)
              { // Thrown by `kdu_error' if new connection had to
                // be dropped due to lack of channel resources.
                kdcs_microsleep(1000000);
                continue;
              }
            break; // Treat all other exceptions as fatal
          }
          if (tcp_channel == NULL)
            {
              owner->num_idle_threads++;
              if (owner->shutdown_in_progress &&
                  (owner->num_idle_threads == owner->num_active_threads))
                monitor->request_closure();
              owner->thread_event.reset();
              owner->thread_event.wait(owner->mutex);
              owner->num_idle_threads--;
              continue;
            }
          jpip_channel =
            new kds_jpip_channel(min_suggested_chunk_bytes,max_chunk_bytes,
                                 max_bytes_per_second);
          jpip_channel->init(monitor,tcp_channel);
          tcp_channel = NULL; // Now owned by the `jpip_channel'
        }
      
      // Now we can communicate with the `jpip_channel' to manage the
      // rest of the connection process, unlocking the `owner' mutex so
      // that other threads can process other incoming connections.
      owner->mutex.unlock();
      try {
        if (process_channel(jpip_channel))
          jpip_channel = NULL;
      }
      catch (kdu_exception) {  }
      if (jpip_channel != NULL)
        {
          jpip_channel->close();
          jpip_channel->release();
        }
      owner->mutex.lock();
    }
  
  owner->num_active_threads--;
  if (owner->num_active_threads == 0)
    {
      owner->finish_event.protected_set();
      monitor->request_closure();
    }
  owner->mutex.unlock();
}

/*****************************************************************************/
/*                    kd_connection_thread::process_channel                  */
/*****************************************************************************/

bool kd_connection_thread::process_channel(kds_jpip_channel *channel)
{
  if (owner->shutdown_in_progress)
    return false;
  channel->set_timeout(max_establishment_seconds,false);
  request_queue.init();
  while ((request_queue.peek_head() != NULL) ||
         channel->retrieve_requests(&request_queue))
    {
      const kd_request *req = request_queue.pop_head();
      if (req == NULL)
        continue; // Should not happen
      send_par.restart();
      if ((req->resource == NULL) &&
          kdcs_has_caseless_prefix(req->method,"JPHT"))
        { // Check if we are connecting an auxiliary channel
          send_par << "JPHT/1.0";
          
          if (source_manager->install_channel(req->method,true,channel,
                                              NULL,send_par))
            return true; // Auxiliary channel successfully off loaded
          else
            return false; // Treat as fatal error; terminate connection.  It
                          // is dangerous to attempt to push `send_par' into
                          // the channel as a reply, because the channel may
                          // well be configured for auxiliary communication
                          // already; anyway, the request was not an HTTP
                          // request so it does not deserve an HTTP reply.
        }
      else if (req->resource == NULL)
        { // Unacceptable method
          send_par << "HTTP/1.1 405 Unsupported request method: \""
                   << req->method << "\"\r\n"
                   << "Connection: close\r\n";
          send_par << "Server: Kakadu JPIP Server "
                   << KDU_CORE_VERSION << "\r\n\r\n";
          channel->push_reply(send_par,0);
          return false; // Safest to terminate connection
        }
      else if (req->resource[0] == '\0')
        { // Empty resource string
          send_par << "HTTP/1.1 400 Request does not contain a valid URI or "
                      "absolute path component (perhaps you omitted the "
                      "leading \"/\" from an absolute path)\r\n"
                   << "Connection: close\r\n";
          send_par << "Server: Kakadu JPIP Server "
                   << KDU_CORE_VERSION << "\r\n\r\n";
          channel->push_reply(send_par,0);
          return false; // Safest to terminate connection
        }
      else if (req->fields.unrecognized != NULL)
        {
          send_par << "HTTP/1.1 400 Unrecognized query field: \""
                   << req->fields.unrecognized << "\"\r\n"
                   << "Connection: close\r\n";
          send_par << "Server: Kakadu JPIP Server "
                   << KDU_CORE_VERSION << "\r\n\r\n";
          channel->push_reply(send_par,0);
          return false; // Safest to terminate connnection
        }
      else if (req->fields.upload != NULL)
        {
          send_par << "HTTP/1.1 501 Upload functionality not "
                      "implemented.\r\n"
                   << "Connection: close\r\n";
          send_par << "Server: Kakadu JPIP Server "
                   << KDU_CORE_VERSION << "\r\n\r\n";
          channel->push_reply(send_par,0);
          return false; // Safest to terminate connection
        }
      else if (((req->fields.channel_id != NULL) ||
                (req->fields.channel_close != NULL)) &&
               (strcmp(req->resource,KD_MY_RESOURCE_NAME) == 0))
        { // Client trying to connect to existing session
          const char *cid = req->fields.channel_id;
          if (cid == NULL)
            cid = req->fields.channel_close;
          send_par << "HTTP/1.1 ";
          request_queue.replace_head(req);  req = NULL;
          if (source_manager->install_channel(cid,false,channel,
                                              &request_queue,send_par))
            return true; // Channel successfully off loaded.
          else
            { // Failure: reason already written to `send_par'
              req = request_queue.pop_head();
              send_par << "\r\nConnection: close\r\n";
              send_par << "Server: Kakadu JPIP Server "
                       << KDU_CORE_VERSION << "\r\n\r\n";
              channel->push_reply(send_par,0);
              return false; // Safest to terminate connection
            }
        }
      else if ((req->fields.admin_key != NULL) &&
               (strcmp(req->fields.admin_key,"0") == 0) &&
               (strcmp(req->resource,KD_MY_RESOURCE_NAME) == 0))
        { // Client requesting delivery of an admin key
          body_data.restart();
          if (source_manager->write_admin_id(body_data))
            {
              body_data << "\r\n";
              int body_bytes = body_data.get_remaining_bytes();
              send_par << "HTTP/1.1 200 OK\r\n";
              send_par << "Cache-Control: no-cache\r\n";
              send_par << "Content-Type: application/text\r\n";
              send_par << "Content-Length: " << body_bytes << "\r\n";
              if (req->close_connection)
                send_par << "Connection: close\r\n";
              send_par << "Server: Kakadu JPIP Server "
                       << KDU_CORE_VERSION << "\r\n\r\n";
              kd_msg << "\n\t"
                        "Key generated for remote admin request.\n\n";
              send_par.append(body_data);
              channel->push_reply(send_par,body_bytes);
            }
          else
            {
              send_par.restart();
              send_par << "HTTP/1.1 403 Remote administration not enabled.\r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n";
              kd_msg << "\n\t"
                        "Admin key requested -- feature not enabled.\n\n";
              send_par << "Server: Kakadu JPIP Server "
                       << KDU_CORE_VERSION << "\r\n\r\n";
              channel->push_reply(send_par,0);
              return false;
            }
        }
      else if ((req->fields.admin_key != NULL) &&
               (strcmp(req->resource,KD_MY_RESOURCE_NAME) == 0))
        {
          if (source_manager->compare_admin_id(req->fields.admin_key))
            process_admin_request(req,channel);
          else
            {
              send_par << "HTTP/1.1 400 Wrong or missing admin "
                          "key as first query field in admin request.\r\n"
                          "Connection: close\r\n";
              kd_msg << "\n\tWrong or missing admin key as "
                        "first query field in admin request.\n\n";
              send_par << "Server: Kakadu JPIP Server "
                       << KDU_CORE_VERSION << "\r\n\r\n";
              channel->push_reply(send_par,0);
              return false;
            }
        }
      else if (req->fields.target != NULL)
        {
          kd_target_description target;
          strncpy(target.filename,req->fields.target,255);
          target.filename[255] = '\0';
          if (req->fields.sub_target != NULL)
            {
              strncpy(target.byte_range,req->fields.sub_target,79);
              target.byte_range[79] = '\0';
            }
          if (restrict_access &&
              ((target.filename[0] == '/') ||
               (strchr(target.filename,':') != NULL) ||
               (strstr(target.filename,"..") != NULL)))
            {
              send_par << "HTTP/1.1 403 Attempting to access "
                          "privileged location on server.\r\n"
                          "Connection: close\r\n"
                          "Server: Kakadu JPIP Server "
                       << KDU_CORE_VERSION << "\r\n\r\n";
              channel->push_reply(send_par,0);
              return false;
            }
          else if (!target.parse_byte_range())
            {
              send_par << "HTTP/1.1 400 Illegal \""
                       << JPIP_FIELD_SUB_TARGET
                       << "\" value string.\r\n"
                          "Connection: close\r\n"
                          "Server: Kakadu JPIP Server "
                       << KDU_CORE_VERSION << "\r\n\r\n";
              channel->push_reply(send_par,0);
              return false;
            }
          else if (((req->fields.channel_new == NULL) &&
                    process_stateless_request(target,req,channel)) ||
                   ((req->fields.channel_new != NULL) &&
                    process_new_session_request(target,req,channel)))
            return true; // Channel has been donated to a source thread or
                         // has already been delegated and released
        }
      else
        {
          send_par << "HTTP/1.1 405 Unsupported request\r\n"
                      "Connection: close\r\n";
          send_par << "Server: Kakadu JPIP Server "
                   << KDU_CORE_VERSION << "\r\n\r\n";
          channel->push_reply(send_par,0);
          return false;
        }
      if (req->close_connection)
        return false;
    }
  return false; // The connection was dropped unexpectedly
}

/*****************************************************************************/
/*               kd_connection_thread::process_admin_request                 */
/*****************************************************************************/

void kd_connection_thread::process_admin_request(const kd_request *req,
                                                 kds_jpip_channel *channel)
{
  const char *command = req->fields.admin_command;
  
  // Extract commands
  bool shutdown = false;
  const char *next;
  char command_buf[80];
  body_data.restart();
  for (; command != NULL; command=next)
    {
      next = command;
      while ((*next != '\0') && (*next != ','))
        next++;
      memset(command_buf,0,80);
      if ((next-command) < 80)
        strncpy(command_buf,command,next-command);
      if (*next == ',')
        next++;
      else
        next = NULL;
      if (strcmp(command_buf,"shutdown") == 0)
        shutdown = true;
      else if (strcmp(command_buf,"stats") == 0)
        source_manager->write_stats(body_data);
      else if (strncmp(command_buf,"history",strlen("history")) == 0)
        {
          int max_records = INT_MAX;
          char *cp = command_buf + strlen("history");
          if (*cp == '=')
            sscanf(cp+1,"%d",&max_records);
          if (max_records > 0)
            source_manager->write_history(body_data,max_records);
        }
    }
  int body_bytes = body_data.get_remaining_bytes();
  
  // Send reply
  send_par.restart();
  send_par << "HTTP/1.1 200 OK\r\n";
  if (req->close_connection)
    send_par << "Connection: close\r\n";
  send_par << "Cache-Control: no-cache\r\n";
  if (body_bytes > 0)
    {
      send_par << "Content-Type: application/text\r\n";
      send_par << "Content-Length: " << body_bytes << "\r\n";
    }      
  send_par << "Server: Kakadu JPIP Server "
           << KDU_CORE_VERSION << "\r\n\r\n";
  send_par.append(body_data);
  channel->push_reply(send_par,body_bytes);
  if (shutdown)
    {
      owner->shutdown_in_progress = true;
      source_manager->close_gracefully();
    }
}

/*****************************************************************************/
/*               kd_connection_thread::process_stateless_request             */
/*****************************************************************************/

bool
  kd_connection_thread::process_stateless_request(kd_target_description &tgt,
                                                  const kd_request *req,
                                                  kds_jpip_channel *channel)
{
  char channel_id[KD_CHANNEL_ID_LEN+1];
  
  send_par.restart();
  send_par << "HTTP/1.1 ";
  request_queue.replace_head(req); // Put the request back on the queue
  if (source_manager->install_stateless_channel(tgt,channel,&request_queue) ||
      (source_manager->create_source_thread(tgt,NULL,owner,
                                            channel_id,send_par) &&
       source_manager->install_channel(channel_id,false,channel,
                                       &request_queue,send_par)))
    {
      kd_msg.start_message();
      kd_msg << "\n\tStateless request accepted\n";
      kd_msg << "\t\tInternal ID: " << channel_id << "\n";
      kd_msg.flush(true);
      return true;
    }

  req = request_queue.pop_head(); // Get the request back again
  if (req->close_connection)
    send_par << "Connection: close\r\n";
  send_par << "\r\nServer: Kakadu JPIP Server "
           << KDU_CORE_VERSION << "\r\n\r\n";
  channel->push_reply(send_par,0);
  kd_msg.start_message();
  kd_msg << "\n\t\tStateless request refused\n";
  kd_msg << "\t\t\tRequested target: \"" << tgt.filename << "\"";
  if (tgt.byte_range[0] != '\0')
    kd_msg << " (" << tgt.byte_range << ")";
  kd_msg << "\n";
  kd_msg.flush(true);
  return false;
}

/*****************************************************************************/
/*             kd_connection_thread::process_new_session_request             */
/*****************************************************************************/

bool
  kd_connection_thread::process_new_session_request(kd_target_description &tgt,
                                                    const kd_request *req,
                                                    kds_jpip_channel *channel)
{
  char channel_id[KD_CHANNEL_ID_LEN+1];
  const char *delegated_host =
    delegator->delegate(req,channel,monitor,max_establishment_seconds,
                        send_par,body_data);
  channel->set_timeout(max_establishment_seconds,false);
        // Give channel a new lease of life after waiting for delegation hosts
        // to respond.
  if (delegated_host != NULL)
    {
      kd_msg.start_message();
      kd_msg << "\n\t\tDelegated session request to host, \""
             << delegated_host << "\"\n";
      kd_msg << "\t\t\tRequested target: \"" << tgt.filename << "\"";
      if (tgt.byte_range[0] != '\0')
        kd_msg << " (" << tgt.byte_range << ")";
      kd_msg << "\n";
      kd_msg << "\t\t\tRequested channel type: \""
             << req->fields.channel_new << "\"\n";
      kd_msg.flush(true);
      channel->close();
      channel->release();
      return true;
    }
  
  send_par.restart();
  send_par << "HTTP/1.1 ";
  const char *cnew = req->fields.channel_new;
  request_queue.replace_head(req); // Put request back on the queue
  if (source_manager->create_source_thread(tgt,cnew,owner,
                                           channel_id,send_par) &&
      source_manager->install_channel(channel_id,false,channel,
                                      &request_queue,send_par))
    {
      kd_msg.start_message();
      kd_msg << "\n\tNew channel request accepted locally\n";
      kd_msg << "\t\tAssigned channel ID: " << channel_id << "\n";
      kd_msg.flush(true);      
      return true;
    }
  req = request_queue.pop_head(); // Get request back again
  send_par << "Cache-Control: no-cache\r\n";
  if (req->close_connection)
    send_par << "Connection: close\r\n";
  send_par << "Server: Kakadu JPIP Server "
           << KDU_CORE_VERSION << "\r\n\r\n";
  channel->push_reply(send_par,0);
  kd_msg.start_message();
  kd_msg << "\n\t\tNew channel request refused\n";
  kd_msg << "\t\t\tRequested target: \"" << tgt.filename << "\"";
  if (tgt.byte_range[0] != '\0')
    kd_msg << " (" << tgt.byte_range << ")";
  kd_msg << "\n";
  kd_msg << "\t\t\tRequested channel type: \""
         << req->fields.channel_new << "\"\n";
  kd_msg.flush(true);
  return false;
}


/* ========================================================================= */
/*                            kd_connection_server                           */
/* ========================================================================= */

/*****************************************************************************/
/*                         kd_connection_server::start                       */
/*****************************************************************************/

void
  kd_connection_server::start(kdcs_sockaddr &listen_address,
                              kdu_uint16 min_udp_port,
                              kdu_uint16 max_udp_port,
                              kdcs_channel_monitor *monitor,
                              kd_delegator *delegator,
                              kd_source_manager *source_manager,
                              int num_threads,
                              int min_suggested_chunk_size,
                              int max_chunk_size,
                              float max_bytes_per_second,
                              float max_establishment_seconds,
                              bool restrict_access)

{
  assert((num_active_threads==0) && (num_idle_threads==0) && (threads==NULL));
  assert(listener == NULL);
  if (!(mutex.exists() && thread_event.exists() && finish_event.exists()))
    { kdu_error e; e << "Failed to create critical synchronization objects.  "
      "Too many threads, perhaps??"; }
  if (num_threads < 1)
    { kdu_error e; e << "The number of connection management threads "
      "must be at least 1!!"; }
  listener = new kdcs_tcp_channel(monitor,true);
  if (!listener->listen(listen_address,5+num_threads,this))
    { kdu_error e; e << "Unable to bind socket to listening address.  You "
      "may need to explicitly specify the IP address via the command line, "
      "or you may need to explicitly supply a port number which is not "
      "currently in use.  If the default HTTP port 80 is in use, port 8080 "
      "is frequently available on many systems."; }
  listener->get_listening_address(host_address);
  
  this->monitor = monitor;
  this->min_suggested_chunk_bytes = min_suggested_chunk_size;
  this->max_chunk_bytes = max_chunk_size;
  this->max_bytes_per_second = max_bytes_per_second;
  this->base_udp_port = min_udp_port;
  assert(udp_port_status == NULL);
  if (max_udp_port < min_udp_port)
    this->num_udp_ports = 0;
  else
    this->num_udp_ports = 1 + (int)(max_udp_port-min_udp_port);
  if (num_udp_ports > 0)
    { 
      udp_port_status = new int[num_udp_ports];
      memset(udp_port_status,0,(size_t)(sizeof(int)*num_udp_ports));
      next_udp_port_idx = 0;
    }
  else
    next_udp_port_idx = -1;
  
  mutex.lock();
  for (; num_threads > 0; num_threads--)
    {
      kd_connection_thread *thread =
        new kd_connection_thread(this,monitor,delegator,source_manager,
                                 min_suggested_chunk_bytes,max_chunk_bytes,
                                 max_bytes_per_second,
                                 max_establishment_seconds,
                                 restrict_access);
      thread->next = threads;
      threads = thread;
      if (!thread->start())
        {
          mutex.unlock();
          kdu_error e; e << "Unable to start requested connection threads!";
        }
      num_active_threads++;
    }
  mutex.unlock();
  
  // Print starting details for log file.
  time_t binary_time; time(&binary_time);
  struct tm *local_time = ::localtime(&binary_time);
  kd_msg << "\nKakadu Experimental JPIP Server "
         << KDU_CORE_VERSION << " started:\n"
         << "\tTime = " << asctime(local_time)
         << "\tListening address:port = "
         << host_address.textualize(KDCS_ADDR_FLAG_LITERAL_ONLY |
                                    KDCS_ADDR_FLAG_BRACKETED_LITERALS)
         << ":" << (int) host_address.get_port() << "\n";
  kd_msg.flush();
}

/*****************************************************************************/
/*                         kd_connection_server::finish                      */
/*****************************************************************************/

void kd_connection_server::finish()
{
  mutex.lock();
  finish_requested = true;
  while ((returned_tail=returned_head) != NULL)
    {
      returned_head = returned_tail->next;
      returned_tail->close();
      returned_tail->release();
    }
  thread_event.protected_set(); // Wake up all the connection threads
  while (num_active_threads > 0)
    {
      finish_event.reset();
      finish_event.wait(mutex);
    }
  mutex.unlock();
  if (listener != NULL)
    {
      listener->close();
      delete listener;
      listener = NULL;
    }
  kd_connection_thread *thrd;
  while ((thrd=threads) != NULL)
    {
      threads = thrd->next;
      delete thrd;
    }  
}

/*****************************************************************************/
/*                    kd_connection_server::return_channel                   */
/*****************************************************************************/

void kd_connection_server::return_channel(kds_jpip_channel *channel)
{
  channel->set_bandwidth_limit(-1.0F); // Removes any local limits
  mutex.lock();
  if (finish_requested || shutdown_in_progress || (num_active_threads == 0))
    {
      mutex.unlock();
      channel->close();
      channel->release();
    }
  else
    {
      channel->next = NULL;
      if (returned_tail != NULL)
        returned_tail = returned_tail->next = channel;
      else
        returned_head = returned_tail = channel;
      thread_event.protected_set();
      mutex.unlock();
    }
}

/*****************************************************************************/
/*                  kd_connection_server::create_udp_channel                 */
/*****************************************************************************/

kds_jpip_channel *
  kd_connection_server::create_udp_channel(kdu_uint16 &the_port)
{
  if ((num_udp_ports == 0) || (next_udp_port_idx < 0))
    return NULL;
  kdcs_udp_channel *udp_channel = new kdcs_udp_channel(monitor,true);
  kdcs_sockaddr tmp_address = host_address;
  mutex.lock();
  bool success = false;
  while ((next_udp_port_idx >= 0) && (next_udp_port_idx < num_udp_ports) &&
         !success)
    { 
      // Start by looking for a port which is free
      int idx = next_udp_port_idx;
      do { 
        if (udp_port_status[idx] == 0)
          break;
        if ((++idx) >= num_udp_ports)
          idx = 0;
      } while (idx != next_udp_port_idx);
      if (udp_port_status[idx] != 0)
        next_udp_port_idx = -1; // No more free ports exist
      else
        { // `idx' identifies a free port
          udp_port_status[idx] = 1; // Reserve the port while mutex unlocked
          if ((next_udp_port_idx = idx+1) >= num_udp_ports)
            next_udp_port_idx = 0;         
          
          mutex.unlock(); // Perform channel binding while unlocked
          tmp_address.set_port(base_udp_port + (kdu_uint16) idx);
          try {
            success = udp_channel->bind(tmp_address,NULL);
          } catch (kdu_exception) {
            udp_channel->close(); // Make sure test below invokes "break"
          }
          mutex.lock(); // Claim the lock again
          if (success)
            the_port = base_udp_port + (kdu_uint16) idx;
          else
            { // Bind attempt failed
              udp_port_status[idx] = -1;
              if (!udp_channel->is_active())
                { // Must be short on some kind of resource; failure transient
                  udp_port_status[idx] = 0;
                  break; // Function can fail here, but leave port available
                }
            }
        }
    }
  mutex.unlock();
  if (!(success && udp_channel->is_active()))
    { 
      delete udp_channel;
      return NULL;
    }
  
  kds_jpip_channel *result =
    new kds_jpip_channel(min_suggested_chunk_bytes,max_chunk_bytes,
                         max_bytes_per_second);
  result->init(monitor,udp_channel);
  return result;
}

/*****************************************************************************/
/*                   kd_connection_server::release_udp_port                  */
/*****************************************************************************/

void
  kd_connection_server::release_udp_port(kdu_uint16 port)
{
  mutex.lock();
  int idx = ((int) port) - ((int) base_udp_port);
  if ((idx >= 0) && (idx < num_udp_ports) && (udp_port_status[idx] > 0))
    { 
      udp_port_status[idx] = 0;
      if (next_udp_port_idx < 0)
        next_udp_port_idx = idx;
    }
  mutex.unlock();
}

/*****************************************************************************/
/*                    kd_connection_server::service_channel                  */
/*****************************************************************************/

void
  kd_connection_server::service_channel(kdcs_channel_monitor *monitor,
                                        kdcs_channel *channel, int cond_flags)
{
  mutex.lock();
  thread_event.protected_set(); // Wake up connection thread to service
                                // the incoming call
  mutex.unlock();
}


/* ========================================================================= */
/*                                kd_delegator                               */
/* ========================================================================= */

/*****************************************************************************/
/*                           kd_delegator::add_host                          */
/*****************************************************************************/

void
kd_delegator::add_host(const char *hostname)
{
  kd_delegation_host *elt = new kd_delegation_host;
  elt->hostname = new char[strlen(hostname)+3];
  strcpy(elt->hostname,hostname);
  elt->load_share = 4;
  char *suffix = strrchr(elt->hostname,'*');
  if (suffix != NULL)
    {
      if ((sscanf(suffix+1,"%d",&(elt->load_share)) != 1) ||
          (elt->load_share < 1))
        {
          delete elt;
          kdu_error e;
          e << "Invalid delegate spec, \"" << hostname << "\", given "
          "on command line.  The `*' character must be followed by a "
          "positive integer load share value.";
        }
      *suffix = '\0';
    }
  elt->port = 80;
  suffix = strrchr(elt->hostname,':');
  if ((suffix != NULL) &&
      ((elt->hostname[0] != '[') || (strchr(suffix,']') == NULL)))
    {
      int port_val = 0;
      if ((sscanf(suffix+1,"%d",&port_val) != 1) ||
          (port_val <= 0) || (port_val >= (1<<16)))
        {
          delete elt;
          kdu_error e;
          e << "Illegal port number suffix found in "
          "delegated host address, \"" << hostname << "\".";
        }
      *suffix = '\0';
      elt->port = (kdu_uint16) port_val;
    }
  if ((elt->hostname[0] != '[') &&
      kdcs_sockaddr::test_ip_literal(elt->hostname,KDCS_ADDR_FLAG_IPV6_ONLY))
    { // Add brackets around IP literal to disambiguate HTTP "Host:" headers
      int c, hostlen = (int) strlen(elt->hostname);
      for (c=hostlen; c > 0; c--)
        elt->hostname[c] = elt->hostname[c-1];
      elt->hostname[0] = '[';
      elt->hostname[hostlen+1] = ']';
      elt->hostname[hostlen+2] = '\0';
    }
  
  elt->hostname_with_port = new char[strlen(elt->hostname)+16]; // Plenty
  sprintf(elt->hostname_with_port,"%s:%d",elt->hostname,elt->port);
  mutex.lock();
  elt->next = head;
  elt->prev = NULL;
  if (head != NULL)
    head = head->prev = elt;
  else
    head = tail = elt;
  num_delegation_hosts++;
  mutex.unlock();
}

/*****************************************************************************/
/*                           kd_delegator::delegate                          */
/*****************************************************************************/

const char *
  kd_delegator::delegate(const kd_request *req,
                         kds_jpip_channel *response_channel,
                         kdcs_channel_monitor *monitor,
                         float max_establishment_seconds,
                         kdcs_message_block &par_block,
                         kdcs_message_block &data_block)
{
  assert(req->resource != NULL);

  // Find the source to use
  int num_connections_attempted = 0;
  int num_resolutions_attempted = 0;
  kd_delegation_host *host = NULL;
  while (1)
    {
      mutex.lock();
      if ((num_resolutions_attempted >= num_delegation_hosts) ||
          (num_connections_attempted > num_delegation_hosts))
        {
          mutex.unlock();
          return NULL;
        }
      for (host = head; host != NULL; host=host->next)
        if ((!host->lookup_in_progress) && (host->load_counter > 0))
          break;
      if (host == NULL)
        { // All load counters down to 0; let's increment them all
          for (host=head; host != NULL; host=host->next)
            if (!host->lookup_in_progress)
              host->load_counter = host->load_share;
          for (host=head; host != NULL; host=host->next)
            if (!host->lookup_in_progress)
              break;
        }
      if (host == NULL)
        { // Waiting for another thread to finish resolving host address
          mutex.unlock();
          kdcs_microsleep(50000); // Sleep for 50 milliseconds
          continue;
        }
      
      // If we get here, we have a host to try.  First, move it to the tail
      // of the service list so that it will be the last host which we or
      // anybody else retries, if something goes wrong.
      if (host->prev == NULL)
        {
          assert(host == head);
          head = host->next;
        }
      else
        host->prev->next = host->next;
      if (host->next == NULL)
        {
          assert(host == tail);
          tail = host->prev;
        }
      else
        host->next->prev = host->prev;
      host->prev = tail;
      if (tail == NULL)
        {
          assert(head == NULL);
          head = tail = host;
        }
      else
        tail = tail->next = host;
      host->next = NULL;
      
      // Now see if we need to resolve the address
      if (host->resolution_counter > 1)
        { // The host has failed before.  Do our best to avoid it for a while.
          host->resolution_counter--;
          mutex.unlock();
          continue;
        }
      if (host->resolution_counter == 1)
        { // Attempt to resolve host name
          host->lookup_in_progress = true;
          mutex.unlock();
          bool success = host->address.init(host->hostname,
                                            KDCS_ADDR_FLAG_BRACKETED_LITERALS);
          mutex.lock();
          if (!success)
            { // Cannot resolve host address
              num_resolutions_attempted++;
              host->resolution_counter = KD_DELEGATE_RESOLUTION_INTERVAL;
              host->lookup_in_progress = false;
              mutex.unlock();
              continue;
            }
          host->address.set_port(host->port);
          host->resolution_counter = 0;
          host->load_counter = host->load_share;
          host->lookup_in_progress = false;
        }
      
      // Now we are ready to use the host
      num_connections_attempted++;
      if (host->load_counter > 0) // May have changed in another thread
        host->load_counter--;
      
      // Copy address before releasing lock, in case it is changed elsewhere.
      kdcs_sockaddr address = host->address;
      mutex.unlock();
      
      // Now try connecting to the host address and performing the
      // relevant transactions.
      kdcs_tcp_channel host_channel(monitor,true);
      host_channel.set_blocking_lifespan(max_establishment_seconds);
      try {
        if (!host_channel.connect(address,NULL)) // Set up for blocking I/O
          { // Cannot connect; may need to resolve address again later on
            fprintf(stderr,"Failed to connect to: %s:%d\n",
                    address.textualize(KDCS_ADDR_FLAG_LITERAL_ONLY |
                                       KDCS_ADDR_FLAG_BRACKETED_LITERALS),
                    (int) address.get_port());
            
            mutex.lock();
            host->resolution_counter = KD_DELEGATE_RESOLUTION_INTERVAL;
            host->load_counter = 0;
            mutex.unlock();
            continue;
          }
        host_channel.get_connected_address(address);
        data_block.restart();
        req->fields.write_query(data_block);
        int body_bytes = data_block.get_remaining_bytes();
        par_block.restart();
        par_block << "POST /" << req->resource << " HTTP/1.1\r\n";
        par_block << "Host: " << host->hostname << ":"
                  << host->port << "\r\n";
        par_block << "Content-Length: " << body_bytes << "\r\n";
        par_block << "Content-Type: application/x-www-form-urlencoded\r\n";
        par_block << "Connection: close\r\n\r\n";
        host_channel.write_block(par_block);
        host_channel.write_block(data_block);
        
        const char *reply, *header;
        
        par_block.restart();
        int code = 503; // Try back later by default
        if ((reply = host_channel.read_paragraph()) != NULL)
          code = process_delegate_response(reply,par_block,address);
        if (code == 503)
          continue; // Host may be too busy to answer right now
        
        // Forward the response incrementally
        bool more_chunks = false;
        int content_length = 0;
        if ((header =
             kdcs_caseless_search(reply,"\nContent-Length:")) != NULL)
          {
            while (*header == ' ') header++;
            sscanf(header,"%d",&content_length);
          }
        else if ((header =
                  kdcs_caseless_search(reply,"\nTransfer-encoding:")) != NULL)
          {
            while (*header == ' ') header++;
            if (kdcs_has_caseless_prefix(header,"chunked"))
              more_chunks = true;
          }
        data_block.restart();
        while (more_chunks || (content_length > 0))
          {
            if (content_length <= 0)
              {
                reply = host_channel.read_line();
                assert(reply != NULL); // Blocking call succeeds or throws
                if ((*reply == '\0') || (*reply == '\n'))
                  continue;
                if ((sscanf(reply,"%x",&content_length) == 0) ||
                    (content_length <= 0))
                  more_chunks = false;
                data_block.set_hex_mode(true);
                data_block << content_length << "\r\n";
                data_block.set_hex_mode(false);
              }
            if (content_length > 0)
              {
                host_channel.read_block(content_length,data_block);
                  // Blocking call succeeds or throws exception
                content_length = 0;
              }
            else
              data_block << "\r\n"; // End of chunked transfer
          }
        
        if ((content_length == 0) && !more_chunks)
          { // Communication with the host is complete
            body_bytes = data_block.get_remaining_bytes();
            par_block.append(data_block);
            response_channel->push_reply(par_block,body_bytes);
            return host->hostname_with_port;
          }
      }
      catch (...) {}
      host_channel.close(); // Not strictly necessary, but could help debugging
      break; // If we get here, we have failed to complete the transaction
    }
  return NULL;
}
