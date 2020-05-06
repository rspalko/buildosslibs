//
//  ViewController.h
//  test_ios
//
//  Created by David Taubman on 13/08/14.
//  Copyright (c) 2014 David Taubman. All rights reserved.
//

#import <UIKit/UIKit.h>
#include "kdu_messaging.h"

/* ========================================================================= */
/*                             Messaging Services                            */
/* ========================================================================= */

class kdu_stream_message : public kdu_core::kdu_thread_safe_message {
  public: // Member classes
    kdu_stream_message(bool throw_exc)
      { this->throw_exc = throw_exc;  msg = @""; }
    ~kdu_stream_message() { msg = nil; }
    void put_text(const char *string)
      { 
        msg = [msg stringByAppendingString:
               [NSString stringWithUTF8String:string]];
      }
    void flush(bool end_of_message=false)
      { 
        kdu_thread_safe_message::flush(end_of_message);
        if (!end_of_message)
          return;
        UIAlertView *alert = [UIAlertView alloc];
        alert = [alert initWithTitle:nil message:msg
                            delegate:nil cancelButtonTitle:@"OK"
                   otherButtonTitles:nil, nil];
        msg = @""; // Start a new message
        [alert show];
        if (throw_exc)
          throw KDU_ERROR_EXCEPTION;
      }
  private: // Data
    NSString *msg;
    bool throw_exc;
  };

/* ========================================================================= */
/*                               ViewController                              */
/* ========================================================================= */

@interface ViewController : UIViewController {
  IBOutlet UITextField *neon_level;
  IBOutlet UITextField *neon_cpus;
  IBOutlet UITextField *neon_threads;
  IBOutlet UIStepper *thread_stepper;
  
  IBOutlet UIButton *compress_button;
  IBOutlet UIButton *compress_reversible_button;
  IBOutlet UIButton *compress_levels_button;
  IBOutlet UIButton *compress_ycc_button;
  IBOutlet UIButton *compress_precise_button;
  IBOutlet UITextField *compress_file_in;
  IBOutlet UITextField *compress_file_out;
  IBOutlet UITextField *compress_status;

  IBOutlet UIButton *expand_button;
  IBOutlet UIButton *expand_input_button;
  IBOutlet UIButton *expand_precise_button;
  IBOutlet UITextField *expand_file_in;
  IBOutlet UITextField *expand_file_out;
  IBOutlet UITextField *expand_status;

  bool compress_reversible;
  float compress_qstep;
  int compress_levels;
  bool compress_ycc;
  bool compress_precise;
  NSString *compress_in_filename;
  NSString *compress_out_filename;
  
  bool expand_precise;
  bool expand_from_compressor_output;
  NSString *expand_in_filename;
  NSString *expand_out_filename;
  
  kdu_stream_message *warn_message;
  kdu_stream_message *error_message;
  kdu_core::kdu_message_formatter *warn_formatter;
  kdu_core::kdu_message_formatter *error_formatter;
}
- (void) dealloc;

- (IBAction) clicked_thread_stepper:(id)sender;

- (IBAction) clicked_compress:(id)sender;
- (IBAction) clicked_compress_reversible:(id)sender;
- (IBAction) clicked_compress_levels:(id)sender;
- (IBAction) clicked_compress_ycc:(id)sender;
- (IBAction) clicked_compress_precise:(id)sender;

- (IBAction) clicked_expand:(id)sender;
- (IBAction) clicked_expand_input:(id)sender;
- (IBAction) clicked_expand_precise:(id)sender;

- (void) derive_filenames;
- (void) update_labels;
- (NSString *) path_from_filename:(NSString *)fname;
  // Prepends the filename with the path to the application's Documents folder

@end
