//
//  ViewController.mm
//  test_ios
//
//  Created by David Taubman on 13/08/14.
//  Copyright (c) 2014 David Taubman. All rights reserved.
//

#import "ViewController.h"
#include <assert.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
#include "kdu_stripe_compressor.h"
#include "kdu_stripe_decompressor.h"
#include "kdu_file_io.h"

using namespace kdu_supp; // Also includes the `kdu_core' namespace

/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                    eat_white_and_comments                          */
/*****************************************************************************/

static void
  eat_white_and_comments(std::istream &stream)
{
  char ch;
  bool in_comment;
  
  in_comment = false;
  while (!(stream.get(ch)).fail())
    if (ch == '#')
      in_comment = true;
    else if (ch == '\n')
      in_comment = false;
    else if ((!in_comment) && (ch != ' ') && (ch != '\t') && (ch != '\r'))
      { 
        stream.putback(ch);
        return;
      }
}

/*****************************************************************************/
/* STATIC                          read_image                                */
/*****************************************************************************/

static kdu_byte *
read_image(const char *fname, int &num_components, int &height, int &width)
  /* Simple PGM/PPM file reader.  Returns an array of interleaved image
     samples (there are 1 or 3 components) in unsigned bytes, appearing
     in scan-line order.  If the file cannot be open, the program simply
     exits for simplicity. */
{
  char magic[3];
  int max_val; // We don't actually use this.
  char ch;
  std::ifstream in(fname,std::ios::in|std::ios::binary);
  if (in.fail())
    { kdu_error e; e << "Unable to open input file, \"" << fname << "\"!"; }
  
  // Read PGM/PPM header
  in.get(magic,3);
  if (strcmp(magic,"P5") == 0)
    num_components = 1;
  else if (strcmp(magic,"P6") == 0)
    num_components = 3;
  else
    { kdu_error e; e << "PGM/PPM image file must start with the magic string, "
      "\"P5\" or \"P6\"!"; }
  eat_white_and_comments(in); in >> width;
  eat_white_and_comments(in); in >> height;
  eat_white_and_comments(in); in >> max_val;
  if (in.fail())
    {kdu_error e; e << "Image file \"" << fname << "\" does not appear to "
      "have a valid PGM/PPM header."; }
  while (!(in.get(ch)).fail())
    if ((ch == '\n') || (ch == ' '))
      break;
  
  // Read sample values
  int num_samples = height*width*num_components;
  kdu_byte *buffer = new kdu_byte[num_samples];
  if ((int)(in.read((char *) buffer,num_samples)).gcount() != num_samples)
    { kdu_error e; e << "PGM/PPM image file \"" << fname
      << "\" terminated prematurely!"; }
  return buffer;
}

/*****************************************************************************/
/* STATIC                         write_image                                */
/*****************************************************************************/

static void
  write_image(const char *fname, kdu_byte *buffer,
              int num_components, int height, int width)
  /* Simple PGM/PPM file writer.  The `buffer' array contains interleaved
     image samples (there are 1 or 3 components) in unsigned bytes,
     appearing in scan-line order. */
{
  std::ofstream out(fname,std::ios::out|std::ios::binary);
  if (!out)
    { kdu_error e;
      e << "Unable to open output image file, \"" << fname << "\"."; }
  if (num_components == 1)
    out << "P5\n" << width << " " << height << "\n255\n";
  else if (num_components == 3)
    out << "P6\n" << width << " " << height << "\n255\n";
  else
    assert(0); // The caller makes sure only 1 or 3 components are decompressed
  if (!out.write((char *) buffer,num_components*width*height))
    { kdu_error e;
      e << "Unable to write to output image file, \"" << fname << "\"."; }
}

/*****************************************************************************/
/* STATIC                         do_compress                                */
/*****************************************************************************/

static double
  do_compress(const char *in_fname, const char *out_fname,
              bool reversible, float qstep, int num_levels,
              bool use_ycc, bool precise, int num_threads)
  /* Catches exceptions, returning -ve on failure.  Otherwise, the function
     returns the number of seconds taken to do the compression. */
{
  double result = 0.0;
  kdu_byte *buffer = NULL;
  kdu_codestream codestream;
  kdu_simple_file_target output;
  kdu_stripe_compressor compressor;
  kdu_thread_env thread_env, *env_ref=NULL;
  try {
    if (num_threads > 1)
      { 
        thread_env.create();  env_ref = &thread_env;
        for (; num_threads > 1; num_threads--)
          env_ref->add_thread();
      }
    int num_components=0, height, width;
    buffer = read_image(in_fname,num_components,height,width);
    kdu_clock timer;
    siz_params siz;
    siz.set(Scomponents,0,0,num_components);
    siz.set(Sdims,0,0,height);
    siz.set(Sdims,0,1,width);
    siz.set(Sprecision,0,0,8);
    siz.set(Ssigned,0,0,false);
    kdu_params *siz_ref = &siz;  siz_ref->finalize();
    output.open(out_fname);
    codestream.create(&siz,&output);
    kdu_params *cod_params=codestream.access_siz()->access_cluster(COD_params);
    cod_params->set(Clevels,0,0,num_levels);
    cod_params->set(Cycc,0,0,use_ycc);
    cod_params->set(Creversible,0,0,reversible);
    kdu_params *qcd_params=codestream.access_siz()->access_cluster(QCD_params);
    if (!reversible)
      qcd_params->set(Qstep,0,0,qstep);
    compressor.start(codestream,0,NULL,NULL,0,false,precise,
                     true,0.05,0,false,env_ref);
    int stripe_heights[3]={height,height,height};
    compressor.push_stripe(buffer,stripe_heights);
    compressor.finish();
    result = timer.get_ellapsed_seconds();
  } catch (kdu_exception exc) {
    result = -1.0;
  } catch (std::bad_alloc) {
    result = -2.0;
  } catch (...) {
    result = -3.0;
  }
  if (buffer != NULL)
    delete[] buffer;
  thread_env.destroy(); // Safe to call this even if never created
  compressor.reset(); // Closes any open tiles that are still around
  if (codestream.exists())
    codestream.destroy();
  output.close();
  return result;
}

/*****************************************************************************/
/* STATIC                          do_expand                                 */
/*****************************************************************************/

static double
  do_expand(const char *in_fname, const char *out_fname, bool precise,
            int num_threads)
  /* Catches exceptions, returning -ve on failure.  Otherwise, the function
     returns the number of seconds taken to do the compression. */
{
  double result = 0.0;
  kdu_simple_file_source input;
  kdu_codestream codestream;
  kdu_stripe_decompressor decompressor;
  kdu_thread_env thread_env, *env_ref=NULL;
  kdu_byte *buffer=NULL;
  kdu_dims dims;
  int num_components=0;
  try {
    if (num_threads > 1)
      { 
        thread_env.create();  env_ref = &thread_env;
        for (; num_threads > 1; num_threads--)
          env_ref->add_thread();
      }    
    kdu_clock timer;
    input.open(in_fname);
    codestream.create(&input);
    codestream.get_dims(0,dims);
    num_components = codestream.get_num_components();
    if (num_components == 2)
      num_components = 1;
    else if (num_components >= 3)
      { // Check that components have consistent dimensions (for PPM file)
        num_components = 3;
        kdu_dims dims1; codestream.get_dims(1,dims1);
        kdu_dims dims2; codestream.get_dims(2,dims2);
        if ((dims1 != dims) || (dims2 != dims))
          num_components = 1;
      }
    codestream.apply_input_restrictions(0,num_components,0,0,NULL);
    buffer = new kdu_byte[(int) dims.area()*num_components];
    decompressor.start(codestream,precise,false,env_ref);
    int stripe_heights[3] = {dims.size.y,dims.size.y,dims.size.y};
    decompressor.pull_stripe(buffer,stripe_heights);
    decompressor.finish();
    result = timer.get_ellapsed_seconds();
  } catch (kdu_exception exc) {
    result = -1.0;
  } catch (std::bad_alloc) {
    result = -2.0;
  } catch (...) {
    result = -3.0;
  }
  thread_env.destroy(); // Safe to call this even if never created
  decompressor.reset();
  if (codestream.exists())
    codestream.destroy();
  input.close();
  if (buffer != NULL)
    { 
      write_image(out_fname,buffer,num_components,dims.size.y,dims.size.x);
      delete[] buffer;
    }
  return result;
}

/* ========================================================================= */
/*                               ViewController                              */
/* ========================================================================= */

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad
{
  warn_formatter = NULL;
  warn_message = NULL;
  error_formatter = NULL;
  warn_formatter = NULL;
  [super viewDidLoad];
	[neon_level
   setText:[NSString stringWithFormat:@"Neon level = %d",
            kdu_get_neon_level()]];
  thread_stepper.minimumValue = 1;
  compress_reversible = true;
  compress_qstep = 0.001F;
  compress_levels = 5;
  compress_ycc = true;
  compress_precise = false;
  compress_in_filename = nil;
  compress_out_filename = nil;
  
  expand_precise = false;
  expand_from_compressor_output = false;
  expand_in_filename = nil;
  expand_out_filename = nil;
  
  [thread_stepper setValue:(NSInteger) 1];
  
  [self derive_filenames];
  [self update_labels];
  [compress_status setText:@"Waiting for compress"];
  [expand_status setText:@"Waiting for expand"];
  
  warn_message = new kdu_stream_message(false);
  error_message = new kdu_stream_message(true);
  warn_formatter = new kdu_message_formatter(warn_message);
  error_formatter = new kdu_message_formatter(error_message);
  kdu_customize_warnings(warn_formatter);
  kdu_customize_errors(error_formatter);
}

- (void)didReceiveMemoryWarning
{
  [super didReceiveMemoryWarning];
}

- (void) dealloc
{
  if (warn_formatter != NULL)
    { delete warn_formatter; warn_formatter = NULL; }
  if (error_formatter != NULL)
    { delete error_formatter; error_formatter = NULL; }
  if (warn_message != NULL)
    { delete warn_message; warn_message = NULL; }
  if (error_message != NULL)
    { delete error_message; error_message = NULL; }
}

- (void) derive_filenames
{
  if (!compress_in_filename)
    compress_in_filename = @"test_in.ppm";
  compress_out_filename = [compress_in_filename stringByDeletingPathExtension];
  if (compress_reversible)
    compress_out_filename =
      [compress_out_filename stringByAppendingFormat:@"_rev_L%d",
       compress_levels];
  else
    compress_out_filename =
      [compress_out_filename stringByAppendingFormat:@"_Q_%03d_L%d",
       (int)(0.5+1000.0*compress_qstep), compress_levels];
  if (!compress_ycc)
    compress_out_filename =
      [compress_out_filename stringByAppendingString:@"_noYCC"];
  compress_out_filename =
    [compress_out_filename stringByAppendingString:
     ((compress_precise)?@"_Cprecise":@"_Cshorts")];
  compress_out_filename =
    [compress_out_filename stringByAppendingString:@".j2c"];
  
  if (expand_from_compressor_output)
    expand_in_filename = compress_out_filename;
  else
    expand_in_filename = @"test_in.j2c";
  expand_out_filename = [expand_in_filename stringByDeletingPathExtension];
  expand_out_filename =
    [expand_out_filename stringByAppendingString:
     ((expand_precise)?@"_Eprecise":@"_Eshorts")];
  expand_out_filename =
    [expand_out_filename stringByAppendingString:@".ppm"];
}

- (void) update_labels
{
  neon_cpus.text = [NSString stringWithFormat:@"Num CPUs = %d",
                    kdu_get_num_processors()];
  neon_threads.text = [NSString stringWithFormat:@"Num Threads = %d",
                       (int)[thread_stepper value]];
  if (compress_reversible)
    [compress_reversible_button setTitle:@"Creversible=yes"
                                     forState:UIControlStateNormal];
  else
    [compress_reversible_button setTitle:
     [NSString stringWithFormat:@"Qstep=%5.3f",compress_qstep]
                                forState:UIControlStateNormal];
  [compress_levels_button setTitle:
   [NSString stringWithFormat:@"Clevels=%d",compress_levels]
                          forState:UIControlStateNormal];
  [compress_ycc_button setTitle:
   ((compress_ycc)?@"Cycc=yes":@"Cycc=no")
                       forState:UIControlStateNormal];
  [compress_precise_button setTitle:
   ((compress_precise)?@"precise":@"shorts")
                           forState:UIControlStateNormal];
  compress_file_in.text = compress_in_filename;
  compress_file_out.text = compress_out_filename;
  
  [expand_precise_button setTitle:
   ((expand_precise)?@"precise":@"shorts")
                         forState:UIControlStateNormal];
  expand_file_in.text = expand_in_filename;
  expand_file_out.text = expand_out_filename;
}

- (NSString *)path_from_filename:(NSString *)fname
{
  NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                       NSUserDomainMask,YES);
  NSString *doc_dir = [paths objectAtIndex:0];
  NSString *result = [doc_dir stringByAppendingFormat:@"/%@", fname];
  return result;
}

- (IBAction)clicked_thread_stepper:(id)sender
{
  [self update_labels];
  [compress_status setText:@"Waiting for compress"];
  [expand_status setText:@"Waiting for expand"];
}

- (IBAction)clicked_compress_reversible:(id)sender
{
  if (compress_reversible)
    { compress_reversible=false; compress_qstep=0.001F; }
  else if (compress_qstep < 0.01F)
    compress_qstep = 0.01F;
  else if (compress_qstep < 0.1F)
    compress_qstep = 0.1F;
  else
    compress_reversible = true;
  [self derive_filenames];
  [self update_labels];
  [compress_status setText:@"Waiting for compress"];
  if (expand_from_compressor_output)
    [expand_status setText:@"Waiting for expand"];
}

- (IBAction)clicked_compress_levels:(id)sender
{
  if (compress_levels == 0)
    compress_levels = 1;
  else if (compress_levels < 5)
    compress_levels = 5;
  else
    compress_levels = 0;
  [self derive_filenames];
  [self update_labels];
  [compress_status setText:@"Waiting for compress"];
  if (expand_from_compressor_output)
    [expand_status setText:@"Waiting for expand"];  
}

- (IBAction)clicked_compress_ycc:(id)sender
{
  compress_ycc = !compress_ycc;
  [self derive_filenames];
  [self update_labels];
  [compress_status setText:@"Waiting for compress"];
  if (expand_from_compressor_output)
    [expand_status setText:@"Waiting for expand"];
}

- (IBAction)clicked_compress_precise:(id)sender
{
  compress_precise = !compress_precise;
  [self derive_filenames];
  [self update_labels];
  [compress_status setText:@"Waiting for compress"];
  if (expand_from_compressor_output)
    [expand_status setText:@"Waiting for expand"];
}

- (IBAction)clicked_expand_precise:(id)sender
{
  expand_precise = !expand_precise;
  [self derive_filenames];
  [self update_labels];
  [expand_status setText:@"Waiting for expand"];
}

- (IBAction)clicked_expand_input:(id)sender
{
  expand_from_compressor_output = !expand_from_compressor_output;
  [self derive_filenames];
  [self update_labels];
  [expand_status setText:@"Waiting for expand"];
}

- (IBAction)clicked_compress:(id)sender
{
  NSString *in_path = [self path_from_filename:compress_in_filename];
  NSString *out_path = [self path_from_filename:compress_out_filename];
  int num_threads = (int)(0.5 + thread_stepper.value);
  double result =
    do_compress([in_path UTF8String],[out_path UTF8String],
                compress_reversible,compress_qstep,compress_levels,
                compress_ycc,compress_precise,num_threads);
  if (result >= 0.0)
    [compress_status setText:
     [NSString stringWithFormat:@"Compressed in %gs", result]];
  else
    [compress_status setText:
     [NSString stringWithFormat:@"Compressor error code: %g!", result]];
  if (expand_from_compressor_output)
    [expand_status setText:@"Waiting for expand"];
}

- (IBAction)clicked_expand:(id)sender
{
  NSString *in_path = [self path_from_filename:expand_in_filename];
  NSString *out_path = [self path_from_filename:expand_out_filename];
  int num_threads = (int)(0.5 + thread_stepper.value);
  double result = do_expand([in_path UTF8String],[out_path UTF8String],
                            expand_precise,num_threads);
  if (result >= 0.0)
    [expand_status setText:
     [NSString stringWithFormat:@"Expanded in %gs", result]];
  else
    [expand_status setText:
     [NSString stringWithFormat:@"Decompressor error code: %g!", result]];
}

@end
