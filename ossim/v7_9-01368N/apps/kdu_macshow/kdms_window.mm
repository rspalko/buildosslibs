/*****************************************************************************/
// File: kdms_window.mm [scope = APPS/MACSHOW]
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
  Implements the main `kdms_window' class and its `kdms_document_view', both
of which are Objective-C classes.   The embedded `kdms_renderer' object is
implemented separately in `kdms_renderer.mm' -- it is a C++ class but
also has to interact with Cocoa.
******************************************************************************/
#import "kdms_window.h"
#import "kdms_catalog.h"
#import "kdms_url_dialog.h"
#import "kdms_metadata_editor.h"
#import <OpenGL/OpenGL.h>
#import <OpenGL/glu.h>

/*===========================================================================*/
/*                            EXTERNAL FUNCTIONS                             */
/*===========================================================================*/

/*****************************************************************************/
/*                             interrogate_user                              */
/*****************************************************************************/

int interrogate_user(const char *message, const char *option0,
                     const char *option1, const char *option2,
                     const char *option3)
{
  NSAlert *alert = [[NSAlert alloc] init];
  [alert setMessageText:[NSString stringWithUTF8String:message]];
  [alert addButtonWithTitle:[NSString stringWithUTF8String:option0]];
  if (option1[0] != '\0')
    {
      [alert addButtonWithTitle:[NSString stringWithUTF8String:option1]];
      if (option2[0] != '\0')
        {
          [alert addButtonWithTitle:[NSString stringWithUTF8String:option2]];
          if (option3[0] != '\0')
            [alert addButtonWithTitle:[NSString stringWithUTF8String:option3]];
        }
    }
  [alert setAlertStyle:NSInformationalAlertStyle];
  NSInteger result = [alert runModal];
  [alert release];
  if (result == NSAlertFirstButtonReturn)
    return 0;
  else if (result == NSAlertSecondButtonReturn)
    return 1;
  else if (result == NSAlertThirdButtonReturn)
    return 2;
  else
    return 3;
}

/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                       kd_time_to_hms                               */
/*****************************************************************************/

void kd_time_to_hms(double time, int &hours, int &minutes, int &seconds)
  /* Converts `time' (measured in seconds) into an hours:minutes:seconds
     representation. */
{
  time = floor(time + 0.5); // Round to nearest whole number of seconds
  hours = (int) floor(time / 3600.0);
  time -= hours*3600.0;
  minutes = (int) floor(time / 60.0);
  if (minutes >= 60)
    minutes = 59; // Just in case
  seconds = (int)(0.5+time-minutes*60.0);
  if (seconds >= 60)
    seconds = 59; // Just in case
}


/*===========================================================================*/
/*                               kdms_scroller                               */
/*===========================================================================*/


@implementation kdms_scroller

/*****************************************************************************/
/*               kdms_scroller:isCompatibleWithOverlayScrollers              */
/*****************************************************************************/

+(BOOL)isCompatibleWithOverlayScrollers
{
  return NO;
}

/*****************************************************************************/
/*                       kdms_scroller:initWithFrame                         */
/*****************************************************************************/

- (id)initWithFrame:(NSRect)frame_rect
{
  fixed_pos = 0.0;
  fixed_size = 0.0;
  old_target = nil;
  old_action = nil;
  unfixed_pos = 0.0;
  unfixed_size = 0.0;
  unfixed_tint = NSDefaultControlTint;
  return [super initWithFrame:frame_rect];
}

/*****************************************************************************/
/*                          kdms_scroller:mouseDown                          */
/*****************************************************************************/

- (void)mouseDown:(NSEvent *)theEvent
{
  if (old_target == nil)
    [super mouseDown:theEvent];
}

/*****************************************************************************/
/*                      kdms_scroller:setKnobProportion                      */
/*****************************************************************************/

- (void)setKnobProportion:(CGFloat)proportion
{
  unfixed_size = (double)proportion;
  if (old_target == nil)
    [super setKnobProportion:proportion];
}

/*****************************************************************************/
/*                        kdms_scroller:setDoubleValue                       */
/*****************************************************************************/

- (void)setDoubleValue:(double)aDouble
{
  unfixed_pos = aDouble;
  if (old_target == nil)
    [super setDoubleValue:aDouble];
}

/*****************************************************************************/
/*                        kdms_scroller:setControlTint                       */
/*****************************************************************************/

- (void)setControlTint:(NSControlTint)tint
{
  unfixed_tint = tint;
  if (old_target == nil)
    [super setControlTint:tint];
}      

/*****************************************************************************/
/*                        kdms_scroller:fixPos:andSize                       */
/*****************************************************************************/

- (void)fixPos:(double)pos andSize:(double)size
{
  if (old_target == nil)
    { // Scroller not currently fixed
      unfixed_pos = [self doubleValue];
      unfixed_size = (double) [self knobProportion];
      unfixed_tint = [self controlTint];
      old_target = [self target];
      old_action = [self action];
      [self setTarget:nil];
      [self setAction:nil];
      [super setControlTint:NSGraphiteControlTint];
    }
  else if ((fixed_pos == pos) && (fixed_size == size))
    return;
  fixed_size = size;
  fixed_pos = pos;
  [super setDoubleValue:fixed_pos];
  [super setKnobProportion:(CGFloat)fixed_size];
}

/*****************************************************************************/
/*                            kdms_scroller:unfix                            */
/*****************************************************************************/

- (void)unfix
{
  if (old_target == nil)
    return; // Nothing to do -- not currently fixed
  fixed_size = fixed_pos = 0.0;
  [self setTarget:old_target];
  [self setAction:old_action];
  old_target = nil;
  old_action = nil;
  [super setKnobProportion:(CGFloat)unfixed_size];
  [super setDoubleValue:unfixed_pos];
  [super setControlTint:unfixed_tint];
}

@end // kdms_scroller


/*===========================================================================*/
/*                            kdms_document_view                             */
/*===========================================================================*/

@implementation kdms_document_view

/*****************************************************************************/
/*                     kdms_document_view:initWithFrame                      */
/*****************************************************************************/

- (id)initWithFrame:(NSRect)frame
{
  renderer = NULL;
  wnd = NULL;
  paint_region_allowed = YES;
  is_focussing = is_dragging = false;
  shape_dragging = shape_scribbling = false;
  mouse_start.x = mouse_start.y = 0.0F;
  last_mouse_shift.x = last_mouse_shift.y = 0.0F;
  mouse_last = mouse_start;
  in_gesture = false;
  in_gesture_drag = false;
  in_gesture_zoom = false;
  target_is_focus = false;
  gesture_stationary_point.x = gesture_stationary_point.y = 0.0F;
  focus_residual_zoom.width = focus_residual_zoom.height = 1.0F;
  is_first_responder = true;
  return [super initWithFrame:frame];
}

/*****************************************************************************/
/*                     kdms_document_view:set_renderer                       */
/*****************************************************************************/

- (void)set_renderer:(kdms_renderer *)the_renderer
{
  renderer = the_renderer;
  paint_region_allowed = YES;
}

/*****************************************************************************/
/*                       kdms_document_view:set_owner                        */
/*****************************************************************************/

- (void)set_owner:(kdms_window *)owner
{
  wnd = owner;
}

/*****************************************************************************/
/*                  kdms_document_view:enablePaintRegion                  */
/*****************************************************************************/

- (void)enablePaintRegion:(BOOL)enable
{
  paint_region_allowed = enable;
}

/*****************************************************************************/
/*                         kdms_document_view::isOpaque                      */
/*****************************************************************************/

- (BOOL)isOpaque
{
  return YES;
}

/*****************************************************************************/
/*                         kdms_document_view:drawRect                       */
/*****************************************************************************/

- (void)drawRect:(NSRect)clipping_rect
{
  if (renderer == NULL)
    return; // Should never happen when drawing is really required.
  
  if (paint_region_allowed)
    { 
      const NSRect *dirty_rects;
      NSInteger i, num_dirty_rects=0;
      [self getRectsBeingDrawn:&dirty_rects count:&num_dirty_rects];
      for (i=0; i < num_dirty_rects; i++)
        { 
          NSGraphicsContext* gc = [NSGraphicsContext currentContext];
          CGContextRef gc_ref = (CGContextRef)[gc graphicsPort];
          renderer->paint_region(dirty_rects[i],gc_ref);
        }
    }
  
  if (renderer->get_shape_editor() != NULL)
    { 
      int n, flags, sel, drag;
      CGFloat pattern[2] = {2.0F,2.0F};
      for (drag=0; drag < 2; drag++)
        { 
          // Draw edges, highlighting selected edges on the correct side
          for (sel=0; sel < ((drag)?2:3); sel++)
            { 
              int mask=-1;
              if (sel == 1)
                mask = JPX_EDITOR_FLAG_SHARED;
              else if (sel == 2)
                mask = JPX_EDITOR_FLAG_SELECTED;
              NSBezierPath *path = [NSBezierPath bezierPath];
              if (drag)
                [path setLineDash:pattern count:2 phase:0.0F];
              [path setLineWidth:(sel==2)?3.0:1.0];
              NSPoint v1, v2;
              for (n=0; (flags=renderer->get_shape_edge(v1,v2,n,(sel==2),
                                                        (drag>0))); n++)
                { 
                  if (((v1.x==v1.y) && (v2.x==v2.y)) || !(flags & mask))
                    continue;
                  v1.x += 0.5F; v1.y += 0.5F;
                  v2.x += 0.5F; v2.y += 0.5F;
                  if (sel == 2)
                    { 
                      double dx=(v2.y-v1.y), dy=-(v2.x-v1.x);
                      double norm = 3.0 / sqrt(dx*dx + dy*dy);
                      v1.x += dx*norm; v2.x += dx*norm;
                      v1.y += dy*norm; v2.y += dy*norm;
                    }
                  [path moveToPoint:v1];
                  [path lineToPoint:v2];
                }
              if (sel == 0)
                [[NSColor whiteColor] setStroke];
              else if (sel == 1)
                [[NSColor grayColor] setStroke];
              else
                [[NSColor yellowColor] setStroke];
              if (n > 0)
                [path stroke];
            }
          
          // Draw ellipses
          NSPoint centre; NSSize extent; double alpha;
          for (n=0;
               (flags=renderer->get_shape_curve(centre,extent,alpha,n,
                                                false,(drag>0)));
               n++)
            { 
              NSBezierPath *path = [NSBezierPath bezierPath];
              if (drag)
                [path setLineDash:pattern count:2 phase:0.0F];
              bool selected = ((flags & JPX_EDITOR_FLAG_SELECTED) != 0);
              [path setLineWidth:(selected)?3.0:1.0];
              NSRect rect;
              centre.x += 0.5; centre.y += 0.5;
              rect.origin.x = centre.x - extent.width;
              rect.origin.y = centre.y - extent.height;
              rect.size.width = 2*extent.width;
              rect.size.height = 2*extent.height;
              [path appendBezierPathWithOvalInRect:rect];
              if (alpha != 0.0)
                {
                  NSAffineTransformStruct xform_struct;
                  xform_struct.m11 = xform_struct.m22 = 1.0;
                  xform_struct.m21 = alpha; xform_struct.m12 = 0.0;
                  xform_struct.tX = -alpha*centre.y; xform_struct.tY = 0.0;
                  NSAffineTransform *xform = [NSAffineTransform transform];
                  [xform setTransformStruct:xform_struct];
                  [path transformUsingAffineTransform:xform];
                }
              if (selected)
                [[NSColor yellowColor] setStroke];
              else
                [[NSColor whiteColor] setStroke];
              [path stroke];
            }
          
          if (!drag)
            { // Draw path outline
              pattern[0] = pattern[1] = 4.0;
              NSBezierPath *path = [NSBezierPath bezierPath];
              [path setLineDash:pattern count:2 phase:0.0F];
              [path setLineWidth:3.0];
              NSPoint v1, v2;
              for (n=0; renderer->get_shape_path(v1,v2,n); n++)
                if ((v1.x!=v1.y) || (v2.x!=v2.y))
                  { 
                    v1.x += 0.5F; v1.y += 0.5F;
                    v2.x += 0.5F; v2.y += 0.5F;
                    [path moveToPoint:v1];
                    [path lineToPoint:v2];
                  }
              if (n > 0)
                {
                  [[NSColor redColor] setStroke];
                  [path stroke];
                }
            }
          
          // Draw anchor points, highlighting the selected anchor point
          for (sel=0; sel < 2; sel++)
            { 
              int mask=(sel>0)?JPX_EDITOR_FLAG_SELECTED:-1;
              NSBezierPath *path = [NSBezierPath bezierPath];
              if (drag)
                [path setLineDash:pattern count:2 phase:0.0F];
              [path setLineWidth:(sel>0)?3.0:1.0];
              NSPoint v;
              for (n=0; (flags=renderer->get_shape_anchor(v,n,(sel>0),
                                                          (drag>0))); n++)
                { 
                  if (!(flags & mask))
                    continue;
                  NSRect rect; rect.origin = v;
                  rect.size.width=rect.size.height=9;
                  rect.origin.x-=4.5F; rect.origin.y-=4.5F;
                  [path appendBezierPathWithOvalInRect:rect];
                }
              if (n > 0)
                { 
                  [[NSColor yellowColor] setStroke];
                  [path stroke];
                }
            }
        }
      
      if (renderer->get_shape_scribble_mode())
        { // Draw scribble path
          NSPoint v;
          if (renderer->get_scribble_point(v,0))
            { 
              NSBezierPath *path = [NSBezierPath bezierPath];
              [path setLineWidth:1.0];
              [path moveToPoint:v];
              for (n=1; renderer->get_scribble_point(v,n); n++)
                [path lineToPoint:v];
              [[NSColor orangeColor] setStroke];
              [path stroke];
            }
        }
    }
}

/*****************************************************************************/
/*                   kdms_document_view:is_first_responder                   */
/*****************************************************************************/

- (bool)is_first_responder
{
  return self->is_first_responder;
}

/*****************************************************************************/
/*                    kdms_document_view:start_focusbox_at                   */
/*****************************************************************************/

- (void)start_focusbox_at:(NSPoint)point
{
  if (renderer == NULL)
    return;
  if (is_focussing || is_dragging)
    [self cancel_focus_drag];
  if (in_gesture)
    [self cancel_gesture_handling];
  mouse_start = mouse_last = point;
  is_focussing = true;
  NSRect rect = [self rectangleFromMouseStartToPoint:point];
  renderer->set_focussing_rect(renderer->convert_region_from_doc_view(rect),
                               true);
}

/*****************************************************************************/
/*                    kdms_document_view:start_viewdrag_at                   */
/*****************************************************************************/

- (void)start_viewdrag_at:(NSPoint)point
{
  if (renderer == NULL)
    return;
  if (is_focussing || is_dragging)
    [self cancel_focus_drag];
  if (in_gesture)
    [self cancel_gesture_handling];
  is_dragging = true;
  mouse_start = mouse_last = [self convertPoint:point toView:nil];
  last_mouse_shift.x = last_mouse_shift.y = 0.0F;
  kdu_dims box;
  target_is_focus =
    ((renderer->get_focus_box(box) > 0) &&
     box.contains(renderer->convert_point_from_doc_view(point)));
  focus_residual_zoom.width = focus_residual_zoom.height = 1.0F;
  if (wnd)
    [wnd set_drag_cursor:YES];
  CGAssociateMouseAndMouseCursorPosition(false);
}

/*****************************************************************************/
/*                    kdms_document_view:cancel_focus_drag                   */
/*****************************************************************************/

- (void)cancel_focus_drag
{
  if (renderer == NULL)
    return;
  if (is_dragging)
    { 
      is_dragging = false;
      last_mouse_shift.x = last_mouse_shift.y = 0.0F;
      CGAssociateMouseAndMouseCursorPosition(true);
      if (wnd)
        [wnd set_drag_cursor:NO];
    }
  if (is_focussing)
    { 
      is_focussing = false;
      renderer->set_focussing_rect(kdu_dims(),false);
      [self invalidate_mouse_box];
    }
  target_is_focus = false;
  if (shape_dragging)
    { 
      jpx_roi_editor *sed = renderer->get_shape_editor();
      if (sed == NULL)
        return;
      kdu_dims update_region = sed->cancel_drag();
      if (!update_region.is_empty())
        {
          [self repaint_shape_region:update_region];
          [renderer->editor update_shape_editor_controls];
        }
    }
  shape_dragging = false;
  if (shape_scribbling)
    {
      jpx_roi_editor *sed = renderer->get_shape_editor();
      if (sed == NULL)
        return;
      kdu_dims update_region = sed->clear_scribble_points();
      if (!update_region.is_empty())
        {
          [self repaint_shape_region:update_region];
          [renderer->editor update_shape_editor_controls];
        }      
    }
  shape_scribbling = false;
}

/*****************************************************************************/
/*                     kdms_document_view:mouse_drag_to                      */
/*****************************************************************************/

- (void)mouse_drag_to:(NSPoint)point
{
  if ((renderer != NULL) && is_focussing)
    { 
      NSRect r = [self rectangleFromMouseStartToPoint:point];
      renderer->set_focussing_rect(renderer->convert_region_from_doc_view(r),
                                   true);
      [self invalidate_mouse_box];
      mouse_last = point;
      [self invalidate_mouse_box];
    }
}

/*****************************************************************************/
/*                     kdms_document_view:mouse_drag_by                      */
/*****************************************************************************/

- (void)mouse_drag_by:(NSPoint)displacement
{
  if ((renderer != NULL) &&
      (is_dragging || in_gesture_drag) &&
      ![wnd check_scrollers_fixed])
    { 
      NSPoint point = mouse_last;
      point.x += displacement.x;
      point.y += displacement.y;
      kdu_coords disp = renderer->convert_point_from_doc_view(point);
      disp -= renderer->convert_point_from_doc_view(mouse_last);
      if (disp.x || disp.y)
        { 
          if (!target_is_focus)
            renderer->scroll_absolute(disp.x,disp.y,false);
          else
            { // Shifting the focus box is more interesting
              disp = renderer->displace_focus_box(disp);
              [self shift_mouse:
               renderer->scale_vector_to_doc_view(disp)];
            }
        }
      mouse_last = point;
    }
}

/*****************************************************************************/
/*                    kdms_document_view:end_focus_drag_at                   */
/*****************************************************************************/

- (bool)end_focus_drag_at:(NSPoint)point
{
  if (renderer == NULL)
    return false;
  if (is_focussing)
    {
      NSRect rect = [self rectangleFromMouseStartToPoint:point];
      [self cancel_focus_drag];
      if ((rect.size.width >= 2.0F) && (rect.size.height >= 2.0F))
        {
          kdu_dims focus_box = renderer->convert_region_from_doc_view(rect);
          if ((focus_box.size.x > 1) && (focus_box.size.y > 1))
            renderer->set_focus_box(focus_box,jpx_metanode());
          return true;
        }
    }
  else if (is_dragging)
    { 
      is_dragging = false;
      target_is_focus = false;
      CGAssociateMouseAndMouseCursorPosition(true);
      last_mouse_shift.x = last_mouse_shift.y = 0.0F;
      if (wnd)
        [wnd set_drag_cursor:NO];
      return true;
    }
  return false;
}

/*****************************************************************************/
/*                     kdms_document_view:shift_mouse                        */
/*****************************************************************************/

- (void)shift_mouse:(NSPoint)displacement
{
  NSPoint mouse = [NSEvent mouseLocation];
  last_mouse_shift = displacement; // subtract from next mouse event we get
  mouse.x += displacement.x;  mouse.y += displacement.y;
  float ms_height = [[[NSScreen screens] objectAtIndex:0] frame].size.height;
  mouse.y = ms_height - mouse.y;
    // It seems that this coordinate transformation always
    // works, even when there are multiple screens with
    // different dimensions, but the screen coordinates
    // used by Quartz and Cocoa are not well documented.
  CGPoint cg_mouse = NSPointToCGPoint(mouse);
  CGWarpMouseCursorPosition(cg_mouse);
}

/*****************************************************************************/
/*             kdms_document_view:rectangleFromMouseStartToPoint             */
/*****************************************************************************/

- (NSRect)rectangleFromMouseStartToPoint:(NSPoint)point
{
  NSRect rect;
  if (mouse_start.x <= point.x)
    {
      rect.origin.x = mouse_start.x;
      rect.size.width = point.x - mouse_start.x + 1.0F;
    }
  else
    {
      rect.origin.x = point.x;
      rect.size.width = mouse_start.x - point.x + 1.0F;
    }
  
  if (mouse_start.y <= point.y)
    {
      rect.origin.y = mouse_start.y;
      rect.size.height = point.y - mouse_start.y + 1.0F;
    }
  else
    {
      rect.origin.y = point.y;
      rect.size.height = mouse_start.y - point.y + 1.0F;
    }
  return rect;
}

/*****************************************************************************/
/*                  kdms_document_view:invalidate_mouse_box                  */
/*****************************************************************************/

- (void)invalidate_mouse_box
{
  NSRect dirty_rect = [self rectangleFromMouseStartToPoint:mouse_last];
  dirty_rect.origin.x -= KDMS_FOCUSSING_MARGIN;
  dirty_rect.size.width += 2.0F*KDMS_FOCUSSING_MARGIN;
  dirty_rect.origin.y -= KDMS_FOCUSSING_MARGIN;
  dirty_rect.size.height += 2.0F*KDMS_FOCUSSING_MARGIN;
  [self setNeedsDisplayInRect:dirty_rect];
}

/*****************************************************************************/
/*                kdms_document_view:cancel_gesture_handling                 */
/*****************************************************************************/

- (void)cancel_gesture_handling
{
  if (in_gesture_zoom && (renderer != NULL))
    renderer->zoom_interactive(1.0,gesture_stationary_point,true);
  in_gesture = false;
  in_gesture_drag = false;
  in_gesture_zoom = false;
  target_is_focus = false;
  focus_residual_zoom.width = focus_residual_zoom.height = 1.0F;
}

/*****************************************************************************/
/*                  kdms_document_view:handle_single_click                   */
/*****************************************************************************/

- (void)handle_single_click:(NSPoint)point
{
  if (renderer == NULL)
    return;
  NSRect ns_rect;
  ns_rect.origin = point;
  ns_rect.size.width = ns_rect.size.height = 0.0F;
  kdu_coords coords = renderer->convert_region_from_doc_view(ns_rect).pos;
  renderer->reveal_metadata_at_point(coords);
}

/*****************************************************************************/
/*                  kdms_document_view:handle_double_click                   */
/*****************************************************************************/

- (void)handle_double_click:(NSPoint)point
{
  if (renderer == NULL)
    return;
  NSRect ns_rect;
  ns_rect.origin = point;
  ns_rect.size.width = ns_rect.size.height = 0.0F;
  kdu_coords coords = renderer->convert_region_from_doc_view(ns_rect).pos;
  renderer->reveal_metadata_at_point(coords,true);
}

/*****************************************************************************/
/*                  kdms_document_view:handle_right_click                    */
/*****************************************************************************/

- (void)handle_right_click:(NSPoint)point
{
  if (renderer == NULL)
    return;
  NSRect ns_rect;
  ns_rect.origin = point;
  ns_rect.size.width = ns_rect.size.height = 0.0F;
  kdu_coords coords = renderer->convert_region_from_doc_view(ns_rect).pos;
  renderer->edit_metadata_at_point(&coords);
}

/*****************************************************************************/
/*              kdms_document_view:handle_shape_editor_click                   */
/*****************************************************************************/

- (void)handle_shape_editor_click:(NSPoint)point
{
  kdu_coords shape_point;
  jpx_roi_editor *sed =
    renderer->map_shape_point_from_doc_view(point,shape_point);
  if (sed == NULL)
    return;
  kdu_dims update_region;
  if (renderer->get_shape_scribble_mode())
    {
      update_region = sed->clear_scribble_points();
      if (!update_region.is_empty())
        [renderer->editor update_shape_editor_controls];
      shape_scribbling = true;
      update_region.augment(sed->add_scribble_point(shape_point));
    }
  else
    {
      if (!sed->find_nearest_anchor(shape_point,false))
        return;
      NSPoint nearest;
      renderer->map_shape_point_to_doc_view(shape_point,nearest);
      double dx=nearest.x-point.x, dy=nearest.y-point.y;
      if ((dx*dx+dy*dy) < 4.5*4.5)
        { 
          shape_dragging = true;
          mouse_start = mouse_last = point;
          update_region = sed->select_anchor(shape_point,true);
        }
      else
        update_region = sed->cancel_selection();
      if (!update_region.is_empty())
        [renderer->editor update_shape_editor_controls];
    }
  if (!update_region.is_empty())
    [self repaint_shape_region:update_region];
}

/*****************************************************************************/
/*              kdms_document_view:handle_shape_editor_drag                  */
/*****************************************************************************/

- (void)handle_shape_editor_drag:(NSPoint)point
{
  kdu_coords shape_point;
  if ([self snap_doc_point:point to_shape:&shape_point])
    {
      jpx_roi_editor *sed = renderer->get_shape_editor();
      kdu_dims update_region = sed->drag_selected_anchor(shape_point);
      if (!update_region.is_empty())
        [self repaint_shape_region:update_region];
      mouse_last = point;
    }
}

/*****************************************************************************/
/*              kdms_document_view:handle_shape_editor_move                  */
/*****************************************************************************/

- (void)handle_shape_editor_move:(NSPoint)point
{
  [self handle_shape_editor_drag:point];
  point = mouse_last; // Ensure we move to the last valid point  
  kdu_coords shape_point;
  shape_dragging = false;
  jpx_roi_editor *sed = renderer->get_shape_editor();
  if (sed == NULL)
    return;
  kdu_dims update_region;
  if (((point.x == mouse_start.x) && (point.y == mouse_start.y)) ||
      ![self snap_doc_point:point to_shape:&shape_point])
    update_region = sed->cancel_drag();
  else
    { 
      update_region = sed->move_selected_anchor(shape_point);
      update_region.augment(renderer->shape_editor_adjust_path_thickness());
    }
  [renderer->editor update_shape_editor_controls];
  if (!update_region.is_empty())
    [self repaint_shape_region:update_region];
}

/*****************************************************************************/
/*              kdms_document_view:add_shape_scribble_point                  */
/*****************************************************************************/

- (void)add_shape_scribble_point:(NSPoint)point
{
  kdu_coords shape_point;
  jpx_roi_editor *sed =
    renderer->map_shape_point_from_doc_view(point,shape_point);
  if (sed != NULL)
    {
      kdu_dims update_region =
        sed->add_scribble_point(shape_point);
      if (!update_region.is_empty())
        [self repaint_shape_region:update_region];
    }
}

/*****************************************************************************/
/*              kdms_document_view:snap_doc_point:to_shape                   */
/*****************************************************************************/

- (bool)snap_doc_point:(NSPoint)point to_shape:(kdu_coords *)snapped_point
{
  kdu_coords shape_point;
  jpx_roi_editor *sed =
    renderer->map_shape_point_from_doc_view(point,shape_point);
  if (sed == NULL)
    return false;
  kdu_coords test_shape_point;
  NSPoint test_doc_point;
  kdu_coords nearest_shape_point = shape_point;
  double min_doc_dist_sq = 20.0; // Snap only if dist^2 is less than this
  double dx, dy, dist_sq;
  
  test_shape_point = shape_point;
  if (sed->find_nearest_anchor(test_shape_point,true))
    { // Consider snapping to a non-selected anchor point
      renderer->map_shape_point_to_doc_view(test_shape_point,test_doc_point);
      dx = test_doc_point.x-point.x;  dy = test_doc_point.y-point.y;
      dist_sq = dx*dx + dy*dy;
      if (dist_sq < min_doc_dist_sq)
        { 
          nearest_shape_point = test_shape_point;
          if (sed->can_move_selected_anchor(nearest_shape_point,false))
            { *snapped_point = nearest_shape_point; return true; }
        }
    }
  
  test_shape_point = shape_point;
  if (sed->find_nearest_boundary_point(test_shape_point,true))
    { // Consider snapping to the boundary of any non-selected region
      renderer->map_shape_point_to_doc_view(test_shape_point,test_doc_point);
      dx = test_doc_point.x-point.x;  dy = test_doc_point.y-point.y;
      dist_sq = dx*dx + dy*dy;
      if (dist_sq < min_doc_dist_sq)
        { min_doc_dist_sq = dist_sq; nearest_shape_point = test_shape_point; }
    }
  
  test_shape_point = shape_point;
  if (sed->find_nearest_guide_point(test_shape_point))
    { // Consider snapping to the nearest guide line -- see definitions
      renderer->map_shape_point_to_doc_view(test_shape_point,test_doc_point);
      dx = test_doc_point.x-point.x;  dy = test_doc_point.y-point.y;
      dist_sq = dx*dx + dy*dy;
      if (dist_sq < min_doc_dist_sq)
        { min_doc_dist_sq = dist_sq; nearest_shape_point = test_shape_point; }
    }
  
  if (!sed->can_move_selected_anchor(nearest_shape_point,false))
    return false;
  *snapped_point = nearest_shape_point;
  return true;
}

/*****************************************************************************/
/*                 kdms_document_view:repaint_shape_region                   */
/*****************************************************************************/

- (void)repaint_shape_region:(kdu_dims) region
{
  kdu_coords min=region.pos, max=min+region.size; min -= kdu_coords(1,1);
  NSPoint p1, p2;
  if ((renderer == NULL) ||
      !(renderer->map_shape_point_to_doc_view(min,p1) &&
        renderer->map_shape_point_to_doc_view(max,p2)))
    return;
  NSRect rect;
  if (p1.x < p2.x)
    { rect.origin.x = p1.x; rect.size.width = p2.x-p1.x+1.0F; }
  else
    { rect.origin.x = p2.x; rect.size.width = p1.x-p2.x+1.0F; }
  if (p1.y < p2.y)
    { rect.origin.y = p1.y; rect.size.height = p2.y-p1.y+1.0F; }
  else
    { rect.origin.y = p2.y; rect.size.height = p1.y-p2.y+1.0F; }
  rect.origin.x -= 7.0F; rect.origin.y -= 7.0F;
  rect.size.width += 14.0F; rect.size.height += 14.0F;
  [self setNeedsDisplayInRect:rect];
}

/*****************************************************************************/
/*                kdms_document_view:acceptsFirstResponder                   */
/*****************************************************************************/

- (BOOL)acceptsFirstResponder
{
  return YES;
}

/*****************************************************************************/
/*                 kdms_document_view:becomeFirstResponder                   */
/*****************************************************************************/

- (BOOL) becomeFirstResponder
{
  self->is_first_responder = true;
  return [super becomeFirstResponder];
}

/*****************************************************************************/
/*                 kdms_document_view:resignFirstResponder                   */
/*****************************************************************************/

- (BOOL) resignFirstResponder
{
  self->is_first_responder = false;
  return [super resignFirstResponder];
}

/*****************************************************************************/
/*                    kdms_document_view:rightMouseDown                      */
/*****************************************************************************/

- (void)rightMouseDown:(NSEvent *)mouse_event
{
  [wnd cancel_pending_show];
  [self cancel_gesture_handling];
  [self cancel_focus_drag];
  NSPoint window_point = [mouse_event locationInWindow];
  NSPoint doc_view_point = [self convertPoint:window_point fromView:nil];
  [self handle_right_click:doc_view_point];
}

/*****************************************************************************/
/*                      kdms_document_view:mouseDown                         */
/*****************************************************************************/

- (void)mouseDown:(NSEvent *)mouse_event
{
  [wnd cancel_pending_show];
  [self cancel_gesture_handling];
  NSUInteger modifier_flags = [mouse_event modifierFlags];
  bool shift_key_down = (modifier_flags & NSShiftKeyMask)?true:false;
  bool ctrl_key_down = (modifier_flags & NSControlKeyMask)?true:false;
  NSPoint window_point = [mouse_event locationInWindow];
  NSPoint doc_view_point = [self convertPoint:window_point fromView:nil];
  shape_dragging = false;
  if (ctrl_key_down)
    [self handle_right_click:doc_view_point];
  else if ((renderer != NULL) && (renderer->get_shape_editor() != NULL))
    [self handle_shape_editor_click:doc_view_point];
  else if (shift_key_down)
    [self start_viewdrag_at:doc_view_point];
  else if ([mouse_event clickCount] == 1)
    [self start_focusbox_at:doc_view_point];
  else
    [self handle_double_click:doc_view_point];
}

/*****************************************************************************/
/*                    kdms_document_view:mouseDragged                        */
/*****************************************************************************/

- (void)mouseDragged:(NSEvent *)mouse_event
{
  NSPoint window_point = [mouse_event locationInWindow];
  NSPoint doc_view_point = [self convertPoint:window_point fromView:nil];
  if ((renderer != NULL) && (renderer->get_shape_editor() != NULL))
    { 
      if (shape_dragging)
        [self handle_shape_editor_drag:doc_view_point];
      else if (shape_scribbling)
        [self add_shape_scribble_point:doc_view_point];
    }
  else if (is_focussing)
    [self mouse_drag_to:doc_view_point];
  else if (is_dragging)
    { // In this case, we have actually disassociated the mouse from the
      // cursor location so we are using mouse movement only to generate
      // displacements that are fed to `mouse_drag_by'.  This makes the code
      // (and perceived behaviour) identical for gesture and mouse control.
      NSPoint delta;
      delta.x = [mouse_event deltaX];
      delta.y = -[mouse_event deltaY];
      delta.x -= last_mouse_shift.x;
      delta.y -= last_mouse_shift.y;
      [self mouse_drag_by:delta];
    }
}

/*****************************************************************************/
/*                        kdms_document_view:mouseUp                         */
/*****************************************************************************/

- (void)mouseUp:(NSEvent *)mouse_event
{
  NSPoint window_point = [mouse_event locationInWindow];
  NSPoint doc_view_point = [self convertPoint:window_point fromView:nil];
  if (is_focussing && ![self end_focus_drag_at:doc_view_point])
    [self handle_single_click:doc_view_point];
  else if (is_dragging)
    [self end_focus_drag_at:doc_view_point];
  if ((renderer != NULL) && (renderer->get_shape_editor() != NULL))
    { 
      if (shape_dragging)
        [self handle_shape_editor_move:doc_view_point];
      else if (shape_scribbling)
        {
          [self add_shape_scribble_point:doc_view_point];
          [renderer->editor update_shape_editor_controls];
          shape_scribbling = false;
        }
    }
  shape_dragging = false;
}

/*****************************************************************************/
/*                 kdms_document_view:beginGestureWithEvent                  */
/*****************************************************************************/

- (void)beginGestureWithEvent:(NSEvent *)event
{
  if (!(is_dragging || is_focussing || shape_dragging || shape_scribbling))
    { // Note the start of the gesture and also figure out whether the gesture
      // is to be applied to the focus box or the view.
      in_gesture = true;
      NSPoint point = [wnd mouseLocationOutsideOfEventStream];
      point = [self convertPoint:point fromView:nil];
      gesture_stationary_point = point;
      kdu_dims box;
      target_is_focus =
        ((renderer->get_focus_box(box) > 0) &&
         box.contains(renderer->convert_point_from_doc_view(point)));
    }
  [super beginGestureWithEvent:event];
}

/*****************************************************************************/
/*                  kdms_document_view:endGestureWithEvent                   */
/*****************************************************************************/

- (void)endGestureWithEvent:(NSEvent *)event
{
  if (in_gesture)
    [self cancel_gesture_handling];
  [super endGestureWithEvent:event];
}

/*****************************************************************************/
/*                    kdms_document_view:magnifyWithEvent                    */
/*****************************************************************************/

- (void)magnifyWithEvent:(NSEvent *)event
{
  double factor = 1.0 + [event magnification];
  if (factor < 0.1)
    factor = 0.1;
  if (renderer != NULL)
    { 
      if (target_is_focus)
        { 
          NSSize fact2;
          fact2.width = factor * focus_residual_zoom.width;
          fact2.height = factor * focus_residual_zoom.height;
          focus_residual_zoom = renderer->scale_focus_box(fact2);
        }
      else
        { 
          renderer->zoom_interactive(factor,gesture_stationary_point,false);
          in_gesture_zoom = true;
        }
    }
}

/*****************************************************************************/
/*                       kdms_document_view:scrollWheel                      */
/*****************************************************************************/

- (void)scrollWheel:(NSEvent *)scroll_event
{
  if ([scroll_event subtype] != NSMouseEventSubtype)
    { 
      if (in_gesture && (renderer != NULL))
        { 
          if (!in_gesture_drag)
            { // Enter the gesture_drag state
              in_gesture_drag = true;
              mouse_last.x = mouse_last.y = 0.0F;
            }          
          else
            { 
              NSPoint delta;
              delta.x = -[scroll_event scrollingDeltaX];
              delta.y = [scroll_event scrollingDeltaY];
              [self mouse_drag_by:delta];
            }
        }
      return;
    }
  else if (in_gesture)
    [self cancel_gesture_handling];
  [super scrollWheel:scroll_event];
}

/*****************************************************************************/
/*                         kdms_document_view:keyDown                        */
/*****************************************************************************/

- (void)keyDown:(NSEvent *)key_event
{
  [self cancel_focus_drag];
  [self cancel_gesture_handling];
  [wnd cancel_pending_show];
  NSUInteger modifier_flags = [key_event modifierFlags];
  if (modifier_flags & NSNumericPadKeyMask)
    {
      bool shift_key_down = (modifier_flags & NSShiftKeyMask)?true:false;
      bool control_key_down = (modifier_flags & NSControlKeyMask)?true:false;
      bool command_key_down = (modifier_flags & NSCommandKeyMask)?true:false;
      NSString *the_arrow = [key_event charactersIgnoringModifiers];
      if (([the_arrow length] > 0) && !command_key_down)
        {
          unichar key_char = [the_arrow characterAtIndex:0];
          kdu_dims shape_regn;
          int na = (control_key_down)?1:10; // Nudge amount
          if (key_char == NSLeftArrowFunctionKey)
            { 
              if (renderer == NULL) return;
              if (!shift_key_down)
                { 
                  if (![wnd check_scrollers_fixed])
                    renderer->scroll_relative_to_view(-0.2F,0.0F);
                }
              else if (renderer->nudge_selected_shape_points(-na,0,shape_regn))
                [self repaint_shape_region:shape_regn];
              else if (renderer->can_do_FocusLeft(nil))
                renderer->menu_FocusLeft();
              return;
            }
          else if (key_char == NSRightArrowFunctionKey)
            { 
              if (renderer == NULL) return;
              if (!shift_key_down)
                { 
                  if (![wnd check_scrollers_fixed])
                    renderer->scroll_relative_to_view(0.2F,0.0F);
                }
              else if (renderer->nudge_selected_shape_points(na,0,shape_regn))
                [self repaint_shape_region:shape_regn];
              else if (renderer->can_do_FocusRight(nil))
                renderer->menu_FocusRight();
              return;
            }
          else if (key_char == NSUpArrowFunctionKey)
            { 
              if (renderer == NULL) return;
              if (!shift_key_down)
                { 
                  if (![wnd check_scrollers_fixed])
                    renderer->scroll_relative_to_view(0.0F,-0.2F);
                }
              else if (renderer->nudge_selected_shape_points(0,-na,shape_regn))
                [self repaint_shape_region:shape_regn];
              else if (renderer->can_do_FocusUp(nil))
                renderer->menu_FocusUp();
              return;
            }
          else if (key_char == NSDownArrowFunctionKey)
            { 
              if (renderer == NULL) return;
              if (!shift_key_down)
                { 
                  if (![wnd check_scrollers_fixed])
                    renderer->scroll_relative_to_view(0.0F,0.2F);
                }
              else if (renderer->nudge_selected_shape_points(0,na,shape_regn))
                [self repaint_shape_region:shape_regn];
              else if (renderer->can_do_FocusDown(nil))
                renderer->menu_FocusDown();
              return;
            }
        }
    }
  [super keyDown:key_event];
}

@end // kdms_document_view


/*===========================================================================*/
/*                                 kdms_glview                               */
/*===========================================================================*/

@implementation kdms_glview

/*****************************************************************************/
/*          kdms_glview:initWithFrame:scrollView:docView:andRenderer         */
/*****************************************************************************/

-(kdms_glview *)initWithFrame:(NSRect)rect
                   scrollView:(NSScrollView *)view
                      docView:(kdms_document_view *)doc
                  andRenderer:(kdms_renderer *)the_renderer
{
  NSOpenGLPixelFormatAttribute attribs[] =
    { NSOpenGLPFAAllRenderers,NSOpenGLPFADoubleBuffer,
      NSOpenGLPFAAccelerated,0};
  NSOpenGLPixelFormat *pixel_format =
    [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];  
  self = [super initWithFrame:rect pixelFormat:pixel_format];
  if (!self)
    return nil;
  [pixel_format release];
  self->scroll_view = view;
  self->doc_view = doc;
  self->renderer = the_renderer;
  self->allow_main_thread_draw = false;
  return self;
}

/*****************************************************************************/
/*                              kdms_glview:dealloc                          */
/*****************************************************************************/

-(void)dealloc
{
  [super dealloc];
}

/*****************************************************************************/
/*                          kdms_glview:prepareOpenGL                        */
/*****************************************************************************/

-(void)prepareOpenGL
{
  [self lock_context];
  [[self openGLContext] makeCurrentContext];
  glClearColor(1.0,1.0,1.0,1.0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_CLAMP);
  glDisable(GL_MULTISAMPLE);
  glDisable(GL_STENCIL_TEST);
  
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
  GLint swap = 1; // Swap during vertical retrace to avoid tearing
  [[self openGLContext] setValues:&swap forParameter:NSOpenGLCPSwapInterval];
  [NSOpenGLContext clearCurrentContext];
  allow_main_thread_draw = true; // To get things started
  [self unlock_context];
}

/*****************************************************************************/
/*                           kdms_glview:lock_context                        */
/*****************************************************************************/

-(void)lock_context
{
  NSOpenGLContext *context = [self openGLContext];
  CGLLockContext((CGLContextObj)[context CGLContextObj]);
}

/*****************************************************************************/
/*                          kdms_glview:unlock_context                       */
/*****************************************************************************/

-(void)unlock_context
{
  NSOpenGLContext *context = [self openGLContext];
  CGLUnlockContext((CGLContextObj)[context CGLContextObj]);
}

/*****************************************************************************/
/*                              kdms_glview:reshape                          */
/*****************************************************************************/

-(void)reshape
{
  [self lock_context];
  NSRect content_frame = [[scroll_view contentView] frame];
  [self setFrame:content_frame];
  [[self openGLContext] makeCurrentContext];
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glViewport(0,0,(int)(0.3+content_frame.size.width),
             (int)(0.3+content_frame.size.height));
  [self unlock_context];
}

/*****************************************************************************/
/*                             kdms_glview:update                            */
/*****************************************************************************/

-(void)update
{
  allow_main_thread_draw = true; // Need this to prevent flicker on resize
  [super update];
}

/*****************************************************************************/
/*                            kdms_glview:drawRect                           */
/*****************************************************************************/

-(void)drawRect:(NSRect)rect
{
  if (!allow_main_thread_draw)
    return;
  allow_main_thread_draw = false;
  [self lock_context];
  if ((renderer == NULL) || !renderer->paint_gl(false))
    glClear(GL_COLOR_BUFFER_BIT);
  [[self openGLContext] flushBuffer];
  [self unlock_context];
}

/*****************************************************************************/
/*                           kdms_glview:isOpaque                            */
/*****************************************************************************/

-(BOOL)isOpaque
{
  return YES;
}

/*****************************************************************************/
/*                     kdms_glview:acceptsFirstResponder                     */
/*****************************************************************************/

- (BOOL)acceptsFirstResponder
{
  return YES;
}

/*****************************************************************************/
/*                      kdms_glview:becomeFirstResponder                     */
/*****************************************************************************/

- (BOOL) becomeFirstResponder
{
  return [[self window] makeFirstResponder:[scroll_view documentView]];
}

/*****************************************************************************/
/*                      kdms_glview:beginGestureWithEvent                    */
/*****************************************************************************/

- (void)beginGestureWithEvent:(NSEvent *)event
{
  [doc_view beginGestureWithEvent:event];
}

/*****************************************************************************/
/*                        kdms_glview:endGestureWithEvent                    */
/*****************************************************************************/

- (void)endGestureWithEvent:(NSEvent *)event
{
  [doc_view endGestureWithEvent:event];
}

/*****************************************************************************/
/*                        kdms_glview:magnifyWithEvent                       */
/*****************************************************************************/

- (void)magnifyWithEvent:(NSEvent *)event
{
  [doc_view magnifyWithEvent:event];
}

@end // kdms_glview

/*===========================================================================*/
/*                             kdms_animation_bar                            */
/*===========================================================================*/

@implementation kdms_animation_bar

/*****************************************************************************/
/*          kdms_animation_bar:initWithFrame:window:andLineHeight            */
/*****************************************************************************/

-(kdms_animation_bar *)initWithFrame:(NSRect)frame_rect
                              window:(kdms_window *)wnd
                            doc_view:(kdms_document_view *)view
                       andLineHeight:(float)height
{
  self->line_height = height;
  self->doc_view = view;
  self->window = wnd;
  target= NULL;
  show_indices = false;
  first_frame = last_frame = false;
  track_interval = 1.0;
  cur_frame_start = cur_frame_end = 0.0;
  slider_start = 0.0;
  slider_end = 0.0;
  slider_interval = 10.0;
  slider_step = 1.0;
  
  play_repeat = false;
  play_fwd = play_rev = false;
  custom_rate = -1.0;
  rate_multiplier = 1.0;
  
  allow_auto_slide = true;
  allow_positioning = true; // We may use this in the future for special
  // sources, for which user positioning of the
  // animation playback point makes no sense.
  
  fwd_play_enabled = rev_play_enabled = stop_play_enabled = true;
  fwd_step_enabled = rev_step_enabled = step_button_hilite = true;
  step_controls_hidden = false;
  
  left_timefield = nil;
  left_timebutton = nil;
  central_slider = nil;
  right_timebutton = nil;
  right_timefield = nil;
  right_timespin = nil;
  
  fwd_playbutton = rev_playbutton = stop_playbutton = nil;
  go_startbutton = go_endbutton = nil;
  fwd_stepbutton = rev_stepbutton = nil;
  step_hilitebox = nil;
  play_ratefield = nil;
  play_ratespin = nil;
  play_nativebutton = nil;
  play_repeatbutton = nil;
  self = [super initWithFrame:frame_rect];
  if (!self)
    return nil;
  
  CGFloat font_size = [NSFont systemFontSizeForControlSize:NSSmallControlSize];
  NSFont *var_font = [NSFont systemFontOfSize:font_size];
  NSFont *fixed_font = [NSFont userFixedPitchFontOfSize:font_size];
  
  [self setTitlePosition:NSNoTitle];
  [self setBoxType:NSBoxCustom];
  [self setBorderType:NSLineBorder];
  [self setBorderWidth:1.0F];
  [self setFillColor:[NSColor controlHighlightColor]];
  NSSize margins; margins.width = margins.height = 2;
  [self setContentViewMargins:margins];
  NSView *box = [self contentView];
  [box setPostsFrameChangedNotifications:YES];
  [[NSNotificationCenter defaultCenter]
   addObserver:self
      selector:@selector(contentBoundsDidChange:)
          name:NSViewFrameDidChangeNotification
        object:box];
  
  // Get ready to lay out controls
  NSRect content_rect = [box frame];
  NSImage *image = nil;
  NSButton *button = nil;
  NSTextField *field = nil;
  NSCell *cell = nil;
  NSStepper *stepper = nil;
  NSRect rect;
  NSSize stepper_size, bitmap_button_size;
  bitmap_button_size.width = bitmap_button_size.height = 15.0F;
  stepper_size.height = line_height;
  stepper_size.width = 0.8F*line_height;

  // Bottom row controls
  rect.origin.x = rect.origin.y = 0.0F;  rect.size = bitmap_button_size;
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  image = [NSImage imageNamed:@"go_startbutton.bmp"];
  button = go_startbutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setImage:image]; [button setImagePosition:NSImageOnly];
  [button setButtonType:NSMomentaryLightButton];  [button setBordered:YES];
  [button setTarget:self];
  [button setAction:@selector(clicked_go_startbutton:)];
  [box addSubview:button];  [button release];
  
  rect.origin.x += rect.size.width + 6;  rect.size = bitmap_button_size;
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  image = [NSImage imageNamed:@"fwd_playbutton.bmp"];
  button = fwd_playbutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setImage:image]; [button setImagePosition:NSImageOnly];
  [button setButtonType:NSMomentaryLightButton];  [button setBordered:YES];
  [button setTarget:self];
  [button setAction:@selector(clicked_fwd_playbutton:)];
  [box addSubview:button];  [button release];
  
  rect.origin.x += rect.size.width + 6;  rect.size = bitmap_button_size;
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  image = [NSImage imageNamed:@"stop_playbutton.bmp"];
  button = stop_playbutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setImage:image]; [button setImagePosition:NSImageOnly];
  [button setButtonType:NSMomentaryLightButton];  [button setBordered:YES];
  [button setTarget:self];
  [button setAction:@selector(clicked_stop_playbutton:)];
  [box addSubview:button];  [button release];
  
  rect.origin.x += rect.size.width + 6;  rect.size = bitmap_button_size;
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  image = [NSImage imageNamed:@"rev_playbutton.bmp"];
  button = rev_playbutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setImage:image]; [button setImagePosition:NSImageOnly];
  [button setButtonType:NSMomentaryLightButton];  [button setBordered:YES];
  [button setTarget:self];
  [button setAction:@selector(clicked_rev_playbutton:)];
  [box addSubview:button];  [button release];
  
  rect.origin.x += rect.size.width + 6;  rect.size = bitmap_button_size;
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  image = [NSImage imageNamed:@"go_endbutton.bmp"];
  button = go_endbutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setImage:image]; [button setImagePosition:NSImageOnly];
  [button setButtonType:NSMomentaryLightButton];  [button setBordered:YES];
  [button setTarget:self];
  [button setAction:@selector(clicked_go_endbutton:)];
  [box addSubview:button];  [button release];

  NSRect hilite_rect = rect;
  hilite_rect.origin.x += rect.size.width + 12;
  hilite_rect.size.width = 12+2*bitmap_button_size.width;
  hilite_rect.size.height = bitmap_button_size.height+4;
  hilite_rect.origin.y = 0.5F*line_height - 0.5F*hilite_rect.size.height;
  hilite_rect.size.height += 6;
  step_hilitebox = [[NSBox alloc] initWithFrame:hilite_rect];
  [step_hilitebox setTitlePosition:NSNoTitle];
  margins.height = margins.width = 0.0F;
  [step_hilitebox setContentViewMargins:margins];
  [step_hilitebox setBoxType:NSBoxCustom];
  [step_hilitebox setBorderType:NSLineBorder];
  [step_hilitebox setBorderWidth:1.0F];
  [step_hilitebox setBorderColor:[NSColor blueColor]];
  [step_hilitebox setAutoresizingMask:0];
  [box addSubview:step_hilitebox];  [step_hilitebox release];
  
  rect = hilite_rect;
  rect.origin.x += 3;  rect.size = bitmap_button_size;
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  image = [NSImage imageNamed:@"rev_stepbutton.bmp"];
  button = rev_stepbutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setImage:image]; [button setImagePosition:NSImageOnly];
  [button setButtonType:NSMomentaryLightButton];  [button setBordered:YES];
  [button setTarget:self];
  [button setAction:@selector(clicked_rev_stepbutton:)];
  [box addSubview:button];  [button release];
  
  rect.origin.x=hilite_rect.origin.x+hilite_rect.size.width-3-rect.size.width;
  image = [NSImage imageNamed:@"fwd_stepbutton.bmp"];
  button = fwd_stepbutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setImage:image]; [button setImagePosition:NSImageOnly];
  [button setButtonType:NSMomentaryLightButton];  [button setBordered:YES];
  [button setTarget:self];
  [button setAction:@selector(clicked_fwd_stepbutton:)];
  [box addSubview:button];  [button release];
  
  rect.origin.x = content_rect.size.width;

  button = play_repeatbutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setBordered:YES];
  [button setButtonType:NSPushOnPushOffButton];
  [button setBezelStyle:NSRoundRectBezelStyle];
  [button setFont:fixed_font];  [button setTitle:@"RPT"];
  rect.size=[[button cell] cellSize];  rect.size.width+=6;
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  rect.origin.x -= rect.size.width;  [button setFrame:rect];
  [button setAutoresizingMask:NSViewMinXMargin]; [box addSubview:button];
  [button setTarget:self];
  [button setAction:@selector(clicked_play_repeatbutton:)];
  [button release];
  
  rect.origin.x -= 6;
  button = play_nativebutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setBordered:YES];
  [button setButtonType:NSPushOnPushOffButton];
  [button setBezelStyle:NSRoundRectBezelStyle];
  [button setFont:var_font];  [button setTitle:@"native"];
  rect.size=[[button cell] cellSize];  rect.size.width+=6;
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  rect.origin.x -= rect.size.width;  [button setFrame:rect];
  [button setAutoresizingMask:NSViewMinXMargin];
  [button setTarget:self];
  [button setAction:@selector(clicked_play_nativebutton:)];
  [box addSubview:button];  [button release];
  
  rect.size = stepper_size;  rect.origin.x -= rect.size.width + 6;
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  stepper = play_ratespin =
    [[NSStepper alloc] initWithFrame:rect];
  [stepper setAutorepeat:NO];
  [stepper setMinValue:-1.0];   [stepper setMaxValue:1.0];
  [[stepper cell] setDoubleValue:0.0];  [stepper setIncrement:1.0];
  [stepper setAutoresizingMask:NSViewMinXMargin];
  [stepper setTarget:self];
  [stepper setAction:@selector(deltapos_play_ratespin:)];
  [box addSubview:stepper]; [stepper release];
  
  field = play_ratefield =
    [[NSTextField alloc] initWithFrame:rect];
  [field setBordered:NO];
  cell = [field cell];  [cell setFont:fixed_font];
  [cell setTitle:@"1000fps"];  rect.size = [cell cellSize];
  rect.origin.y = 0.5F*line_height - 0.5F*rect.size.height;
  rect.origin.x -= rect.size.width;
  [field setBackgroundColor:[self fillColor]];
  [field setAutoresizingMask:NSViewMinXMargin];  [field setEditable:NO];
  [field setFrame:rect];  [box addSubview:field];  [field release];
  
  // Top row controls
  rect.origin.x = 0.0F;
  field = left_timefield =
    [[NSTextField alloc] initWithFrame:rect];
  [field setBordered:NO];
  cell = [field cell];  [cell setFont:fixed_font];
  [cell setTitle:@"00:00:00"];  rect.size = [cell cellSize];
  [cell setAlignment:NSRightTextAlignment];
  rect.origin.y = 1.5F*line_height - 0.5F*rect.size.height;
  [field setBackgroundColor:[self fillColor]];  [field setEditable:NO];
  [field setFrame:rect];  [box addSubview:field];  [field release];
  
  rect.origin.x += rect.size.width + 4;  rect.size = bitmap_button_size;
  rect.origin.y = 1.5F*line_height - 0.5F*rect.size.height;
  image = [NSImage imageNamed:@"left_timebutton.bmp"];
  button = left_timebutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setImage:image]; [button setImagePosition:NSImageOnly];
  [button setButtonType:NSMomentaryLightButton];  [button setBordered:YES];
  [button setTarget:self];
  [button setAction:@selector(clicked_left_timebutton:)];
  [box addSubview:button];  [button release];

  NSRect slider_rect; // This one is going to have to be dynamically adapted
  slider_rect.origin.x = rect.origin.x + rect.size.width + 4;
  
  rect.origin.x = content_rect.size.width;

  rect.size = stepper_size;  rect.origin.x -= rect.size.width;
  rect.origin.y = 1.5F*line_height - 0.5F*rect.size.height;
  stepper = right_timespin =
    [[NSStepper alloc] initWithFrame:rect];
  [stepper setAutorepeat:NO];
  [stepper setMinValue:-1.0];    [stepper setMaxValue:1.0];
  [[stepper cell] setDoubleValue:0.0];  [stepper setIncrement:1.0];
  [stepper setAutoresizingMask:NSViewMinXMargin];
  [stepper setTarget:self];
  [stepper setAction:@selector(deltapos_right_timespin:)];
  [box addSubview:stepper]; [stepper release];
  
  field = right_timefield =
    [[NSTextField alloc] initWithFrame:rect];
  [field setBordered:NO];
  cell = [field cell];  [cell setFont:fixed_font];
  [cell setTitle:@"+10min"];  rect.size = [cell cellSize];
  rect.origin.y = 1.5F*line_height - 0.5F*rect.size.height;
  rect.origin.x -= rect.size.width;
  [field setBackgroundColor:[self fillColor]];
  [field setAutoresizingMask:NSViewMinXMargin];  [field setEditable:NO];
  [field setFrame:rect];  [box addSubview:field];  [field release];
  
  rect.size = bitmap_button_size;  rect.origin.x -= rect.size.width + 4;
  rect.origin.y = 1.5F*line_height - 0.5F*rect.size.height;
  image = [NSImage imageNamed:@"right_timebutton.bmp"];
  button = right_timebutton =
    [[NSButton alloc] initWithFrame:rect];
  [button setImage:image]; [button setImagePosition:NSImageOnly];
  [button setButtonType:NSMomentaryLightButton];  [button setBordered:YES];
  [button setAutoresizingMask:NSViewMinXMargin];
  [button setTarget:self];
  [button setAction:@selector(clicked_right_timebutton:)];
  [box addSubview:button];  [button release];
  
  slider_rect.size.width = rect.origin.x - 4 - slider_rect.origin.x;
  slider_rect.size.height = line_height-2;
  slider_rect.origin.y = 1.5F*line_height - 0.5F*slider_rect.size.height;
  central_slider =
    [[NSSlider alloc] initWithFrame:slider_rect];
  [central_slider setTickMarkPosition:NSTickMarkAbove];
  [central_slider setNumberOfTickMarks:10];
  [central_slider setMinValue:0.0];
  [central_slider setMaxValue:1.0];
  [central_slider setTarget:self];
  [central_slider setAction:@selector(moved_sliderthumb:)];
  [box addSubview:central_slider];  [central_slider release];

  step_controls_hidden = false;
  [self reset];
  
  return self;
}

/*****************************************************************************/
/*                       kdms_animation_bar:dealloc                          */
/*****************************************************************************/

-(void)dealloc
{
  if (left_timefield)
    { [left_timefield removeFromSuperview]; left_timefield=nil; }
  if (left_timebutton)
    { [left_timebutton removeFromSuperview]; left_timebutton=nil; }
  if (central_slider)
    { [central_slider removeFromSuperview]; central_slider=nil; }
  if (right_timebutton)
    { [right_timebutton removeFromSuperview]; right_timebutton=nil; }
  if (right_timefield)
    { [right_timefield removeFromSuperview]; right_timefield=nil; }
  if (right_timespin)
    { [right_timespin removeFromSuperview]; right_timespin=nil; }

  if (fwd_playbutton)
    { [fwd_playbutton removeFromSuperview]; fwd_playbutton=nil; }
  if (go_startbutton)
    { [go_startbutton removeFromSuperview]; go_startbutton=nil; }
  if (stop_playbutton)
    { [stop_playbutton removeFromSuperview]; stop_playbutton=nil; }
  if (go_endbutton)
    { [go_endbutton removeFromSuperview]; go_endbutton=nil; }
  if (rev_playbutton)
    { [rev_playbutton removeFromSuperview]; rev_playbutton=nil; }
  if (fwd_stepbutton)
    { [fwd_stepbutton removeFromSuperview]; fwd_stepbutton=nil; }
  if (rev_stepbutton)
    { [rev_stepbutton removeFromSuperview]; rev_stepbutton=nil; }
  if (step_hilitebox)
    { [step_hilitebox removeFromSuperview]; step_hilitebox=nil; }
  if (play_ratefield)
    { [play_ratefield removeFromSuperview]; play_ratefield=nil; }
  if (play_ratespin)
    { [play_ratespin removeFromSuperview]; play_ratespin=nil; }
  if (play_nativebutton)
    { [play_nativebutton removeFromSuperview]; play_nativebutton=nil; }
  if (play_repeatbutton)
    { [play_repeatbutton removeFromSuperview]; play_repeatbutton=nil; }

  [[NSNotificationCenter defaultCenter]
   removeObserver:self];

  [super dealloc];
}

/*****************************************************************************/
/*                kdms_animation_bar:contentBoundsDidChange                  */
/*****************************************************************************/

-(void)contentBoundsDidChange:(NSNotification *)notification
{
  float content_width = [[self contentView] frame].size.width;
  NSRect rect = [central_slider frame];
  rect.size.width = [right_timebutton frame].origin.x - 4 - rect.origin.x;
  if (rect.size.width < 0.0F)
    { 
      [right_timebutton setHidden:YES];
      [right_timefield setHidden:YES];
      [right_timespin setHidden:YES];
    }
  else
    { 
      [right_timebutton setHidden:NO];
      [right_timefield setHidden:NO];
      [right_timespin setHidden:NO];    
    }
  if (rect.size.width < (0.25F*content_width))
    [central_slider setHidden:YES];
  else
    { 
      [central_slider setHidden:NO];
      [central_slider setFrame:rect];
    }
  
  // See if we have space to place the right-aligned controls
  rect = [go_endbutton frame];
  float left_edge = rect.origin.x+rect.size.width+6;
  float right_edge = [play_ratefield frame].origin.x;
  if (left_edge > right_edge)
    { 
      [play_ratefield setHidden:YES];
      [play_ratespin setHidden:YES];
    }
  else
    { 
      [play_ratefield setHidden:NO];
      [play_ratespin setHidden:NO];
    }
  
  if (left_edge > [play_nativebutton frame].origin.x)
    [play_nativebutton setHidden:YES];
  else
    [play_nativebutton setHidden:NO];
  
  if (left_edge > [play_repeatbutton frame].origin.x)
    [play_repeatbutton setHidden:YES];
  else
    [play_repeatbutton setHidden:NO];
  
  // Finally, see if we have space to place the step controls in the centre
  right_edge -= 6;
  rect = [step_hilitebox frame];
  if (rect.size.width > (right_edge-left_edge))
    { 
      step_controls_hidden = true;
      [fwd_stepbutton setHidden:YES];
      [rev_stepbutton setHidden:YES];
      [step_hilitebox setHidden:YES];
    }
  else
    { 
      step_controls_hidden = false;
      rect.origin.x = left_edge + 0.5F*(right_edge-left_edge-rect.size.width);
      [step_hilitebox setFrameOrigin:rect.origin];
      left_edge = rect.origin.x;  right_edge = left_edge + rect.size.width;
      rect = [rev_stepbutton frame];
      rect.origin.x = left_edge+3;
      [rev_stepbutton setFrame:rect];
      rect = [rev_stepbutton frame];
      rect.origin.x = right_edge-rect.size.width-3;
      [fwd_stepbutton setFrame:rect];
      if (step_button_hilite)
        [step_hilitebox setHidden:NO];
      [fwd_stepbutton setHidden:NO];
      [rev_stepbutton setHidden:NO];
    }
  
  [self setNeedsDisplay:YES];
}

/*****************************************************************************/
/*                       kdms_animation_bar:set_target                       */
/*****************************************************************************/

-(void)set_target:(kdms_renderer *)renderer can_position:(bool)allow
{
  self->target = renderer;
  self->allow_positioning = allow;
}

/*****************************************************************************/
/*                         kdms_animation_bar:reset                          */
/*****************************************************************************/

-(void)reset
{
  target = NULL;
  custom_rate = rate_multiplier = -1.0; // So `set_play_fwd...' does something
  [self set_play_fwd:false rev:false repeat:false fps:-1.0 multiplier:1.0];
  show_indices = false;
  first_frame = last_frame = false;
  cur_frame_start = cur_frame_end = 0.0;
  track_interval = -1.0;
  slider_start = slider_end = 0.0;
  slider_interval = 10.0;
  slider_step = 1.0;
  allow_positioning = true;
  [[left_timefield cell] setTitle:@"00:00:00"];
  [[right_timefield cell] setTitle:@"+10sec"];
  [left_timebutton setEnabled:YES];
  [central_slider setEnabled:YES];
  [right_timebutton setEnabled:YES];
  [fwd_playbutton setEnabled:YES];
  [go_startbutton setEnabled:YES];
  [go_endbutton setEnabled:YES];
  [rev_playbutton setEnabled:YES];
  [stop_playbutton setEnabled:NO];
  [fwd_stepbutton setEnabled:YES];
  [rev_stepbutton setEnabled:YES];
  [step_hilitebox setHidden:YES];
  fwd_play_enabled = rev_play_enabled = true;
  stop_play_enabled = false;
  fwd_step_enabled = rev_step_enabled = true;
  step_button_hilite = false;
}

/*****************************************************************************/
/*                 kdms_animation_bar:update_play_controls                   */
/*****************************************************************************/

-(void)update_play_controls
{
  bool at_end = last_frame ||
    ((track_interval > 0.0) && (cur_frame_end >= track_interval));
  bool at_start = first_frame || (cur_frame_start <= 0.0);
  bool new_fwd_enabled = !(play_fwd || (at_end && !play_repeat));
  bool new_rev_enabled = !(play_rev || (at_start && !play_repeat));
  if (new_fwd_enabled != fwd_play_enabled)
    { 
      fwd_play_enabled = new_fwd_enabled;
      [fwd_playbutton setEnabled:(fwd_play_enabled)?YES:NO];
    }
  if (new_rev_enabled != rev_play_enabled)
    { 
      rev_play_enabled = new_rev_enabled;
      [rev_playbutton setEnabled:(rev_play_enabled)?YES:NO];
    }
  bool new_stop_enabled = play_fwd || play_rev;
  if (new_stop_enabled != stop_play_enabled)
    { 
      stop_play_enabled = new_stop_enabled;
      [stop_playbutton setEnabled:(stop_play_enabled)?YES:NO];
    }
  if (step_button_hilite && (cur_frame_start < cur_frame_end))
    { 
      step_button_hilite = false;
      [step_hilitebox setHidden:YES];
    }
  else if ((cur_frame_start >= cur_frame_end) && !step_button_hilite)
    { 
      step_button_hilite = true;
      if (!step_controls_hidden)
        [step_hilitebox setHidden:NO];
    }
  bool can_step_fwd = allow_positioning && !last_frame;
  if (fwd_step_enabled != can_step_fwd)
    { 
      fwd_step_enabled = can_step_fwd;
      [fwd_stepbutton setEnabled:(fwd_step_enabled)?YES:NO];
    }
  bool can_step_bwd = allow_positioning && !first_frame;
  if (rev_step_enabled != can_step_bwd)
    { 
      rev_step_enabled = can_step_bwd;
      [rev_stepbutton setEnabled:(rev_step_enabled)?YES:NO];
    }  
}

/*****************************************************************************/
/*         kdms_animation_bar:set_play_fwd:rev:repeat:fps:multiplier         */
/*****************************************************************************/

-(void)set_play_fwd:(bool)playing_forward
                rev:(bool)playing_reverse
             repeat:(bool)repeat_enabled
                fps:(double)custom_fps
         multiplier:(double)native_rate_multiplier
{
  if ((self->play_repeat == repeat_enabled) &&
      (self->play_fwd == playing_forward) &&
      (self->play_rev == playing_reverse) &&
      (self->custom_rate == custom_fps) &&
      (self->rate_multiplier == native_rate_multiplier))
    return; // Nothing to do

  self->play_repeat = repeat_enabled;
  self->play_fwd = playing_forward;
  self->play_rev = playing_reverse;
  self->custom_rate = custom_fps;
  self->rate_multiplier = native_rate_multiplier;
  
  [play_repeatbutton setState:(play_repeat)?NSOnState:NSOffState];
  [self update_play_controls];
  char text[20];
  if (custom_rate < 0.0)
    { 
      [play_nativebutton setState:NSOnState];
      if (rate_multiplier >= 100.0)
        sprintf(text,"x%1.0f",rate_multiplier);
      else if (rate_multiplier >= 10.0)
        sprintf(text,"x%#01.1f",rate_multiplier);
      else
        sprintf(text,"x%#01.2f",rate_multiplier);
    }
  else
    { 
      [play_nativebutton setState:NSOffState];
      if (custom_rate >= 100.0)
        sprintf(text,"%#1.0ffps",custom_rate);
      else if (custom_rate >= 10.0)
        sprintf(text,"%#01.1ffps",custom_rate);
      else
        sprintf(text,"%#01.2ffps",custom_rate);
    }
  [[play_ratefield cell] setTitle:[NSString stringWithUTF8String:text]];
}
                            
/*****************************************************************************/
/*         kdms_animation_bar:set_frame:max:start:end:track_duration         */
/*****************************************************************************/

-(void)set_frame:(int)frame_idx
             max:(int)max_frame_idx
           start:(double)frame_start_time
             end:(double)frame_end_time
  track_duration:(double)track_end_time;
{
  if (frame_idx < 0)
    frame_idx = 0; // Just in case
  else if ((max_frame_idx >= 0) && (frame_idx > max_frame_idx))
    frame_idx = max_frame_idx; // Just in case
  if (frame_end_time < frame_start_time)
    frame_end_time = frame_start_time; // Just in case
  if ((track_end_time > 0.0) && (frame_end_time > track_end_time))
    frame_end_time = track_end_time; // Just in case
  if (frame_start_time > frame_end_time)
    frame_start_time = frame_end_time; // Just in case
      
  bool force_update = !show_indices;
  if (frame_start_time < 0.0)
    { 
      show_indices = true;
      cur_frame_start = (double) frame_idx;
      cur_frame_end = cur_frame_start + 1.0;
    }
  else
    { 
      show_indices = false;
      cur_frame_start = frame_start_time;
      cur_frame_end = frame_end_time;
      if (track_end_time < 0.0)
        max_frame_idx = -1;
    }
  bool was_first=first_frame, was_last=last_frame;
  self->first_frame = (frame_idx == 0);
  self->last_frame = (frame_idx == max_frame_idx);
  if ((was_first!=first_frame) || (was_last!=last_frame))
    force_update = true;
  if (force_update)
    slider_interval = [self get_natural_slider_interval:0
                                               and_step:slider_step];
  if (max_frame_idx <= 0)
    { 
      if (track_interval > 0.0)
        force_update = true;
      track_interval = -1.0;
    }
  else
    { 
      if (track_interval != track_end_time)
        force_update = true;
      track_interval = track_end_time;
      if (show_indices)
        track_interval = (double)(max_frame_idx+1);
      double new_val, new_step;
      while ((track_interval <=
              (new_val=[self get_natural_slider_interval:-1
                                                and_step:new_step])) &&
             (slider_interval != new_val))
        { slider_interval = new_val; slider_step = new_step; }
    }
  [self update_controls_for_track_pos:force_update];
}

/*****************************************************************************/
/*         kdms_animation_bar:get_natural_slider_interval:and_step           */
/*****************************************************************************/

-(double)get_natural_slider_interval:(int)adjust and_step:(double &)step
{
  double natural_vals[4], natural_steps[4];
  if (show_indices)
    { 
      natural_vals[0] = 10.0;    natural_vals[1] = 100.0;
      natural_vals[2] = 1000.0;  natural_vals[3] = 10000.0;
      natural_steps[0] = 1.0;    natural_steps[1] = 10.0;
      natural_steps[2] = 100.0;  natural_steps[3] = 1000.0;
    }
  else
    { 
      natural_vals[0] = 2.0;     natural_vals[1] = 10.0;
      natural_vals[2] = 60.0;    natural_vals[3] = 600.0;
      natural_steps[0] = 1.0;    natural_steps[1] = 1.0;
      natural_steps[2] = 10.0;   natural_steps[3] = 60.0;      
    }
  int best_idx = 0;
  if (adjust == 0)
    { // Want closest
      for (int n=1; n < 4; n++)
        { 
          double best_delta = slider_interval - natural_vals[best_idx];
          double delta = slider_interval - natural_vals[n];
          delta = (delta < 0.0)?-delta:delta;
          best_delta = (best_delta < 0.0)?-best_delta:best_delta;
          if (delta < best_delta)
            best_idx = n;
        }
    }
  else if (adjust > 0)
    { // Want next larger interval
      for (int n=0; n < 4; n++)
        { 
          best_idx = n;
          if (slider_interval < natural_vals[n])
            break;
        }
    }
  else
    { // Want next smaller interval
      for (int n=3; n >= 0; n--)
        { 
          best_idx = n;
          if (slider_interval > natural_vals[n])
            break;
        }
    }
  step = natural_steps[best_idx];
  return natural_vals[best_idx];
}

/*****************************************************************************/
/*             kdms_animation_bar:update_controls_for_track_pos              */
/*****************************************************************************/

-(void)update_controls_for_track_pos:(bool)force_update
{
  // Save control state so we can avoid unnecessary updates
  double old_slider_start=slider_start;
  double old_slider_end=slider_end;
  
  // Adjust slider start position to be a multiple of 0.5*slider_interval so
  // that `pos' lies at least one `slider_step' from the slider ends, if
  // possible.
  double min_threshold=slider_start, max_threshold=slider_end;
  if (self->allow_auto_slide)
    { 
      double gap = 0.15*(slider_end-slider_start);
      if (gap > slider_step)
        gap = slider_step;
      min_threshold += gap;
      max_threshold -= gap;
    }
  if (slider_step > 0.0)
    { 
      double slider_inc=slider_step*floor(0.5+0.5*slider_interval/slider_step);
      if ((slider_end <= slider_start) ||
          (cur_frame_start < min_threshold) ||
          (cur_frame_start > max_threshold))
        { 
          slider_start=slider_inc*(floor(0.5+cur_frame_start/slider_inc)-1.0);
          if (slider_start < 0.0)
            slider_start = 0.0;
          slider_end = slider_start + slider_interval;
        }
      if (track_interval > 0.0)
        { 
          if (slider_end >= (track_interval+slider_inc))
            { slider_start -= slider_inc; slider_end -= slider_inc; }
          if (slider_start <= 0.0)
            { 
              slider_start = 0.0;
              while (slider_end >= (track_interval+slider_step))
                slider_end -= slider_step;
            }
        }
    }
    
  // Modify the controls
  double slider_range = slider_end - slider_start;
  if ((slider_start != old_slider_start) ||
      (slider_end != old_slider_end) || force_update)
    { 
      char start_text[20], end_text[20];
      if (show_indices)
        { 
          sprintf(start_text,"%d",(int)(1.5+slider_start));
          sprintf(end_text,"+%d",(int)(0.5+slider_range));
        }
      else
        { 
          int hours, minutes, seconds;
          kd_time_to_hms(slider_start,hours,minutes,seconds);
          sprintf(start_text,"%02d:%02d:%02d",hours,minutes,seconds);
          kd_time_to_hms(slider_range,hours,minutes,seconds);
          minutes += 60*hours; // There should be no hours
          if (minutes == 0)
            sprintf(end_text,"+%-2dsec",seconds);
          else
            sprintf(end_text,"+%-2dmin",minutes);
        }
      [self draw_slider_ticks];
      [[left_timefield cell]
       setTitle:[NSString stringWithUTF8String:start_text]];
      [[right_timefield cell]
       setTitle:[NSString stringWithUTF8String:end_text]];
      [left_timebutton setEnabled:(allow_positioning &&
                                   (slider_start > 0.0))?YES:NO];
      [right_timebutton setEnabled:(allow_positioning &&
                                    ((slider_end < track_interval) ||
                                     (track_interval <= 0.0)))?YES:NO];
      [go_startbutton setEnabled:(allow_positioning && !first_frame)?YES:NO];
      [go_endbutton setEnabled:(allow_positioning && !last_frame)?YES:NO];
    }
  
  if ((slider_range == 0.0) || first_frame)
    [[central_slider cell] setDoubleValue:0.0]; // Should not happen
  else
    { 
      double rel_pos = (cur_frame_start-slider_start) / slider_range;
      [[central_slider cell] setDoubleValue:rel_pos];
    }
  
  [self update_play_controls];
}

/*****************************************************************************/
/*                  kdms_animation_bar:draw_slider_ticks                     */
/*****************************************************************************/

-(void)draw_slider_ticks
{
  int num_ticks = 0;
  if (slider_step > 0.0)
    num_ticks = 1 + (int) floor(0.5 + (slider_end-slider_start) / slider_step);
  [central_slider setNumberOfTickMarks:num_ticks];
}

/*****************************************************************************/
/*                   kdms_animation_bar:moved_sliderthumb                    */
/*****************************************************************************/

-(IBAction)moved_sliderthumb:(NSSlider *)sender
{
  if ((target == NULL) || !allow_positioning)
    return;
  double slider_range = slider_end - slider_start;
  if (slider_range <= 0.0)
    return;
  double rel_pos = [[central_slider cell] doubleValue];
  if (rel_pos > 1.0)
    rel_pos = 1.0; // Should not be possible
  else if (rel_pos < 0.0)
    rel_pos = 0.0; // Also should not be possible
  double tgt_frame_start = (rel_pos * slider_range) + slider_start;
  bool old_auto_slide = self->allow_auto_slide;
  self->allow_auto_slide = false;
  if (show_indices)
    target->set_animation_point_by_idx((int)(0.5+tgt_frame_start));
  else if (tgt_frame_start <= 0.0)
    target->set_animation_point_by_idx(0);
  else if ((track_interval > 0.0) && (tgt_frame_start >= track_interval))
    target->set_animation_point_by_idx(-1);
  else
    target->set_animation_point_by_time(tgt_frame_start,true);
  self->allow_auto_slide = old_auto_slide;
}

/*****************************************************************************/
/*                kdms_animation_bar:deltapos_right_timespin                 */
/*****************************************************************************/

-(IBAction)deltapos_right_timespin:(NSStepper *)sender
{
  double val = [[sender cell] doubleValue];
  if (val < -0.5)
    { 
      [[sender cell] setDoubleValue:0.0];
      double new_val, new_step;
      new_val = [self get_natural_slider_interval:-1 and_step:new_step];
      if (new_val != slider_interval)
        { 
          slider_interval = new_val;
          slider_step = new_step;
          slider_start = slider_end = 0.0;
          [self update_controls_for_track_pos:true];
        }
    }
  else if (val > 0.5)
    { 
      [[sender cell] setDoubleValue:0.0];
      if ((track_interval <= 0.0) || (slider_interval < track_interval))
        { 
          double new_val, new_step;
          new_val = [self get_natural_slider_interval:1 and_step:new_step];
          if (new_val != slider_interval)
            { 
              slider_interval = new_val;
              slider_step = new_step;
              slider_start = slider_end = 0.0;
              [self update_controls_for_track_pos:true];
            }
        }
    }
}

/*****************************************************************************/
/*                kdms_animation_bar:deltapos_play_ratespin                  */
/*****************************************************************************/

-(IBAction)deltapos_play_ratespin:(NSStepper *)sender
{
  if (target == NULL)
    return;
  double val = [[sender cell] doubleValue];
  if (val < -0.5)
    target->menu_PlayFrameRateDown();
  else if (val > 0.5)
    target->menu_PlayFrameRateUp(); 
  else
    return;
  [[sender cell] setDoubleValue:0.0];
}

/*****************************************************************************/
/*                kdms_animation_bar:clicked_fwd_playbutton                  */
/*****************************************************************************/

-(IBAction)clicked_fwd_playbutton:(NSButton *)sender
{
  if (target != NULL)
    target->menu_PlayStartForward();
}

/*****************************************************************************/
/*                kdms_animation_bar:clicked_rev_playbutton                  */
/*****************************************************************************/

-(IBAction)clicked_rev_playbutton:(NSButton *)sender
{
  if (target != NULL)
    target->menu_PlayStartBackward();
}

/*****************************************************************************/
/*                kdms_animation_bar:clicked_fwd_stepbutton                  */
/*****************************************************************************/

-(IBAction)clicked_fwd_stepbutton:(NSButton *)sender
{
  if ((target == NULL) || !allow_positioning)
    return;
  target->menu_PlayStop();
  target->menu_NavImageNext();
}

/*****************************************************************************/
/*                kdms_animation_bar:clicked_rev_stepbutton                  */
/*****************************************************************************/

-(IBAction)clicked_rev_stepbutton:(NSButton *)sender
{
  if ((target == NULL) || !allow_positioning)
    return;
  target->menu_PlayStop();
  target->menu_NavImagePrev();
}

/*****************************************************************************/
/*                kdms_animation_bar:clicked_stop_playbutton                 */
/*****************************************************************************/

-(IBAction)clicked_stop_playbutton:(NSButton *)sender
{
  if (target != NULL)
    target->menu_PlayStop();
}

/*****************************************************************************/
/*                kdms_animation_bar:clicked_go_startbutton                  */
/*****************************************************************************/

-(IBAction)clicked_go_startbutton:(NSButton *)sender
{
  if ((target == NULL) || !allow_positioning)
    return;
  target->set_animation_point_by_idx(0);
}

/*****************************************************************************/
/*                 kdms_animation_bar:clicked_go_endbutton                   */
/*****************************************************************************/

-(IBAction)clicked_go_endbutton:(NSButton *)sender
{
  if ((target == NULL) || !allow_positioning)
    return;
  target->set_animation_point_by_idx(-1);
}

/*****************************************************************************/
/*                kdms_animation_bar:clicked_left_timebutton                 */
/*****************************************************************************/

-(IBAction)clicked_left_timebutton:(NSButton *)sender
{
  if ((target == NULL) || !allow_positioning)
    return;
  double tgt_frame_start = cur_frame_start - slider_interval;
  if (tgt_frame_start < 0.0)
    tgt_frame_start = 0.0;
  if (show_indices)
    target->set_animation_point_by_idx((int)(0.5+tgt_frame_start));
  else
    target->set_animation_point_by_time(tgt_frame_start,true);
}

/*****************************************************************************/
/*               kdms_animation_bar:clicked_right_timebutton                 */
/*****************************************************************************/

-(IBAction)clicked_right_timebutton:(NSButton *)sender
{
  if ((target == NULL) || !allow_positioning)
    return;
  double tgt_frame_start = cur_frame_start + slider_interval;
  if (show_indices)
    target->set_animation_point_by_idx((int)(0.5+tgt_frame_start));
  else
    target->set_animation_point_by_time(tgt_frame_start,true);
}

/*****************************************************************************/
/*               kdms_animation_bar:clicked_play_repeatbutton                */
/*****************************************************************************/

-(IBAction)clicked_play_repeatbutton:(NSButton *)sender
{
  if (target != NULL)
    target->menu_PlayRepeat();
}

/*****************************************************************************/
/*               kdms_animation_bar:clicked_play_nativebutton                */
/*****************************************************************************/

-(IBAction)clicked_play_nativebutton:(NSButton *)sender
{
  if (target == NULL)
    return;
  if (custom_rate < 0.0)
    target->menu_PlayCustom();
  else
    target->menu_PlayNative();
}

@end // kdms_animation_bar


/*===========================================================================*/
/*                                kdms_window                                */
/*===========================================================================*/

@implementation kdms_window

/*****************************************************************************/
/*                 kdms_window:initWithManager:andCursors                    */
/*****************************************************************************/

- (kdms_window *)initWithManager:(kdms_window_manager *)manager
                      andCursors:(NSCursor **)cursors
{
  int p; // Used to index status panes
  
  // Initialize the private members to empty values for the moment
  window_manager = nil; // Until we are sure everything has worked
  renderer = NULL;
  doc_view = nil;
  hor_scroller = vert_scroller = nil;
  scroll_view = nil;
  glview = nil;
  
  scroller_mutex.create();
  scrollers_fixed = false;
  hor_scroller_fixed_pos = 0.0;
  hor_scroller_fixed_size = 0.0;
  vert_scroller_fixed_pos = 0.0;
  vert_scroller_fixed_size = 0.0;
  
  status_bar = nil;
  manager_cursors = cursors;
  doc_cursor_normal = cursors[0];
  doc_cursor_dragging = cursors[1];
  status_height = 0.0F;
  for (p=0; p < 3; p++)
    {
      status_panes[p] = nil;
      status_pane_lengths[p] = 0;
      status_pane_caches[p] = NULL;
    }
  progress_indicator = nil;
  progress_height = 0;
  
  catalog_panel = nil;
  catalog_tools = nil;
  catalog_paste_bar = nil;
  catalog_paste_clear = nil;
  catalog_buttons[0] = catalog_buttons[1] = catalog_buttons[2] = nil;
  catalog_panel_width = 0.0F;
  catalog_tools_height = 0.0F;
  animation_bar = nil;
  animation_line_height = animation_height = 0;
  
  // Find an initial frame location and size for the window
  NSRect screen_rect = [[NSScreen mainScreen] frame];
  NSRect frame_rect; 
  frame_rect.origin.x = frame_rect.origin.y = 0.0F;
  frame_rect.size.width = (float) ceil(screen_rect.size.width * 0.3);
  frame_rect.size.height = (float) ceil(screen_rect.size.height * 0.3);
  NSUInteger window_style = NSTitledWindowMask | NSClosableWindowMask |
                            NSMiniaturizableWindowMask |
                            NSResizableWindowMask;
  NSRect content_rect = [NSWindow contentRectForFrameRect:frame_rect
                         styleMask:window_style];
                         
  // Create window and link it in
  self = [super initWithContentRect:content_rect
                          styleMask:window_style
                            backing:NSBackingStoreBuffered defer:NO];
  if (!self)
    return nil;
  manager->add_window(self);
  window_manager = manager; // Now we are on manager's list, we can record it
  
  // Get content view
  NSView *super_view = [self contentView];
  content_rect = [super_view frame]; // Get the relative view rectangle; size
              // should be the same, but origin is now relative to the window.

  // Determine the font, colours and dimensions for the status panes
  CGFloat font_size = [NSFont systemFontSizeForControlSize:NSSmallControlSize];
  status_font = [NSFont systemFontOfSize:font_size];
  [status_font retain];
  status_pane_height = (float)
    ceil([status_font ascender] - [status_font descender] + 6.0F);
  NSColor *status_background_colour =
    [NSColor colorWithCalibratedRed:0.95F green:0.95F blue:1.0F alpha:1.0F];
  NSColor *status_text_colour =
    [NSColor colorWithCalibratedRed:0.3F green:0.1F blue: 0.0F alpha:1.0F];
  
  // Create status bar and panes
  status_height = status_pane_height;
  NSRect status_bar_rect = content_rect;
  status_bar_rect.size.height = status_height;
  status_bar =
    [[NSView alloc] initWithFrame:status_bar_rect];
  [status_bar setAutoresizingMask:(NSViewWidthSizable | NSViewMinXMargin)];
  [super_view addSubview:status_bar];
  NSRect pane_rects[3];
  for (p=0; p < 3; p++)
    {
      pane_rects[p] = content_rect;
      pane_rects[p].size.height = status_pane_height;
    }
  pane_rects[0].size.width = pane_rects[2].size.width =
    (float) floor(content_rect.size.width * 0.3F);
  pane_rects[1].size.width = content_rect.size.width -
    2 * pane_rects[0].size.width;
  pane_rects[1].origin.x += pane_rects[0].size.width;
  pane_rects[2].origin.x = pane_rects[1].origin.x + pane_rects[1].size.width;
  for (p=0; p < 3; p++)
    { 
      NSTextField *pane = status_panes[p] =
        [[NSTextField alloc] initWithFrame:pane_rects[p]];
      [pane setBezeled:YES];
      [pane setBezelStyle:NSTextFieldSquareBezel];
      [pane setEditable:NO];
      [pane setDrawsBackground:YES];
      [pane setBackgroundColor:status_background_colour];
      [pane setTextColor:status_text_colour];
      [pane setAutoresizingMask:(NSViewWidthSizable | NSViewMaxYMargin |
                                 NSViewMinXMargin | NSViewMaxXMargin)];
      NSTextFieldCell *cell = [pane cell];
      [cell setFont:status_font];
      [status_bar addSubview:pane];
      [pane release];
    }
  
  // Create animation bar, but don't place it yet
  animation_line_height = 1.3F*status_pane_height;
  animation_height = 0.0F;
  NSRect animation_bar_rect = content_rect;
  animation_bar_rect.size.height = 2*animation_line_height;
  animation_bar =
    [[kdms_animation_bar alloc] initWithFrame:animation_bar_rect
                                       window:self
                                     doc_view:doc_view
                                andLineHeight:animation_line_height];
    
  // Create `NSScrollView' for the window
  NSRect scroll_view_rect = content_rect;
  scroll_view_rect.origin.y += pane_rects[0].size.height;
  scroll_view_rect.size.height -= pane_rects[0].size.height;
  int resizing_mask = NSViewWidthSizable | NSViewHeightSizable;
  scroll_view =
    [[NSScrollView alloc] initWithFrame:scroll_view_rect];
  [scroll_view setBorderType:NSNoBorder];
  [scroll_view setAutoresizingMask:resizing_mask];
  NSRect hor_scroller_rect = scroll_view_rect;
  hor_scroller_rect.size.height = 0.1*hor_scroller_rect.size.width;
  hor_scroller =
    [[kdms_scroller alloc] initWithFrame:hor_scroller_rect];
  NSRect vert_scroller_rect = scroll_view_rect;
  vert_scroller_rect.size.width = 0.1*vert_scroller_rect.size.height;
  vert_scroller =
    [[kdms_scroller alloc] initWithFrame:vert_scroller_rect];
  [scroll_view setVerticalScroller:vert_scroller];
  [scroll_view setHorizontalScroller:hor_scroller];
  [scroll_view setHasVerticalScroller:YES];
  [scroll_view setHasHorizontalScroller:YES];
  [[scroll_view contentView] setCopiesOnScroll:YES];
  [super_view addSubview:scroll_view];
  
  // Create `renderer' and `kdms_document_view' objects for the window
  doc_view =
    [[kdms_document_view alloc] initWithFrame:content_rect];
  renderer = new kdms_renderer(self,doc_view,scroll_view,manager);
  [doc_view set_renderer:renderer];
  [doc_view set_owner:self];
  [scroll_view setDocumentView:doc_view];
  [scroll_view setDocumentCursor:doc_cursor_normal];
  
  // Find the minimum size the window can have
  NSRect min_rect;
  min_rect.origin.x = min_rect.origin.y = 0.0F;
  min_rect.size.height = [self get_bottom_margin_height] + 40.0F;
  min_rect.size.width = [self get_right_margin_width] + 40.0F;
  min_rect = [self frameRectForContentRect:min_rect];
  [self setMinSize:min_rect.size];  
  
  // Set things up to receive notifications of changes in the `scroll_view'.
  [[NSNotificationCenter
            defaultCenter] addObserver:self
                              selector:@selector(scroll_view_BoundsDidChange:)
                                  name:NSViewBoundsDidChangeNotification
                                object:[scroll_view contentView]];
  [[NSNotificationCenter
            defaultCenter] addObserver:self
                              selector:@selector(scroll_view_BoundsDidChange:)
                                  name:NSViewFrameDidChangeNotification
                                object:[scroll_view contentView]];
    
  // Release local references and display the window
  renderer->close_file(); // Causes the correct title to be written
  [status_bar release]; // Retained by the window's content view
  [self center];
  [self makeKeyAndOrderFront:self]; // Display the window
  return self;
}

/*****************************************************************************/
/*                             kdms_window:dealloc                           */
/*****************************************************************************/

- (void)dealloc
{
  if (window_manager != NULL)
    { 
      kdms_window_manager *manager = window_manager;
      window_manager = nil;
      manager->remove_window(self);
    }
  if (doc_view)
    { 
      [doc_view set_renderer:NULL];
      [doc_view set_owner:nil];
      [doc_view release];
      doc_view = nil;
    }
  if (scroll_view)
    { 
      [scroll_view release];
      scroll_view = nil;
    }
  if (renderer != NULL)
    { delete renderer; renderer = NULL; }
  if (animation_bar)
    { 
      [animation_bar set_target:NULL can_position:false];
      [animation_bar release];
      animation_bar = nil;
    }
  if (status_font)
    { 
      [status_font release];
      status_font = nil;
    }
  if (hor_scroller)
    { 
      [hor_scroller release];
      hor_scroller = nil;
    }
  if (vert_scroller)
    { 
      [vert_scroller release];
      vert_scroller = nil;
    }
  scroller_mutex.destroy();
  [super dealloc];
}

/*****************************************************************************/
/*                           kdms_window:open_file                           */
/*****************************************************************************/

- (void)open_file:(NSString *)fname
{
  if (renderer == NULL)
    return;
  if (renderer->have_unsaved_edits &&
      (interrogate_user("You have edited the existing file but not saved "
                        "these edits ... perhaps you saved the file in a "
                        "format which cannot hold all the metadata ("
                        "use JPX).  Do you still want to close the existing "
                        "file to open a new one?",
                        "Cancel","Close without Saving") == 0))
      return;
  const char *filename = [fname UTF8String];
  if (window_manager->check_open_file_replaced(filename) &&
      !interrogate_user("A file you have elected to open is out of date, "
                        "meaning that another open window in the application "
                        "has saved over the file but not yet released its "
                        "access, so that the saved copy can replace the "
                        "original.  Do you still wish to open the old copy?",
                        "Cancel","Open Anyway"))
    return;
  renderer->open_file(filename);
}

/*****************************************************************************/
/*                           kdms_window:open_url                            */
/*****************************************************************************/

- (void)open_url:(NSString *)url
{
  if (renderer == NULL)
    return;
  if (renderer->have_unsaved_edits &&
      (interrogate_user("You have edited the existing file but not saved "
                        "these edits ... perhaps you saved the file in a "
                        "format which cannot hold all the metadata ("
                        "use JPX).  Do you still want to close the existing "
                        "file to open a new one?",
                        "Cancel","Close without Saving") == 0))
    return;
  const char *filename_or_url = [url UTF8String];
  renderer->open_file(filename_or_url);
}

/*****************************************************************************/
/*                kdms_window:note_jpip_client_reconnecting                  */
/*****************************************************************************/

-(void)note_jpip_client_reconnecting:(kdu_supp::kdu_client *)client
{
  if (renderer != NULL)
    renderer->note_jpip_client_reconnecting(client);
}

/*****************************************************************************/
/*                   kdms_window:application_can_terminate                   */
/*****************************************************************************/

- (bool)application_can_terminate
{
  if (renderer == NULL)
    return true;
  if ((renderer->editor != nil) &&
      (interrogate_user("You have an open metadata editor for one or more "
                        "windows that will be closed when this application "
                        "terminates. Are you sure you want to quit the "
                        "application?",
                        "Cancel","Quit Application") != 1))
    return false;
  if (renderer->have_unsaved_edits &&
      (interrogate_user("You have unsaved edits in one or more windows that "
                        "will be closed when this application terminates.  "
                        "Are you sure you want to quit the application?",
                        "Cancel","Quit Application") != 1))
    return false;
  return true;
}

/*****************************************************************************/
/*                    kdms_window:application_terminating                    */
/*****************************************************************************/

- (void)application_terminating
{
  if (renderer != NULL)
    renderer->perform_essential_cleanup_steps(true);
}

/*****************************************************************************/
/*                             kdms_window:close                             */
/*****************************************************************************/

- (void) close
{
  if (renderer != NULL)
    { 
      if ((renderer->editor != nil) &&
          (interrogate_user("You have an open metadata editor, which will "
                            "be closed if you continue.  Do you still want "
                            "to close the window?",
                            "Cancel","Continue with Close") == 0))
        return;
      if (renderer->have_unsaved_edits &&
          (interrogate_user("You have edited this file but not saved these "
                            "edits ... perhaps you saved the file in a "
                            "format which cannot hold all the metadata ("
                            "use JPX).  Do you still want to close?",
                            "Cancel","Close without Saving") == 0))
        return;
      if (renderer->metashow)
        [renderer->metashow close];
      if (renderer->editor)
        [renderer->editor close];
      if (renderer->catalog_source)
        renderer->set_metadata_catalog_source(nil); // So it gets released
      renderer->close_file();
    }
  
  if (renderer != NULL)
    { delete renderer; renderer = NULL; }
  if (animation_bar)
    { 
      assert(animation_height == 0.0f);
      [animation_bar set_target:NULL can_position:false];
      [animation_bar release];
      animation_bar = nil;
    }
  
  if (window_manager != NULL)
    { 
      kdms_window_manager *manager = window_manager;
      window_manager = nil;
      manager->remove_window(self);
    }
  if (doc_view)
    { 
      [doc_view set_renderer:NULL];
      [doc_view set_owner:nil];
      [doc_view release];
      doc_view = nil;
    }
  if (scroll_view)
    { 
      [scroll_view release];
      scroll_view = nil;
    }
  if (status_font)
    { 
      [status_font release];
      status_font = nil;
    }
  if (hor_scroller)
    { 
      [hor_scroller release];
      hor_scroller = nil;
    }
  if (vert_scroller)
    { 
      [vert_scroller release];
      vert_scroller = nil;
    }
  
  [super close];  
}

/*****************************************************************************/
/*                     kdms_window:check_scrollers_fixed                     */
/*****************************************************************************/

- (bool)check_scrollers_fixed
{
  return scrollers_fixed;
}

/*****************************************************************************/
/*                     kdms_window:fixScrollerPos:andSize                    */
/*****************************************************************************/

- (void)fixScrollerPos:(NSPoint)pos andSize:(NSSize)size
{
  if (scrollers_fixed &&
      (hor_scroller_fixed_pos == (double)pos.x) &&
      (hor_scroller_fixed_size == (double)size.width) &&
      (vert_scroller_fixed_pos == (double)pos.y) &&
      (vert_scroller_fixed_size == (double)size.height))
    return; // Nothing to do; changes may only be made on this thread
  scroller_mutex.lock();
  scrollers_fixed = true;
  hor_scroller_fixed_pos = (double)pos.x;
  hor_scroller_fixed_size = (double)size.width;
  vert_scroller_fixed_pos = (double)pos.y;
  vert_scroller_fixed_size = (double)size.height;
  scroller_mutex.unlock();
  [self performSelectorOnMainThread:@selector(applyScrollerFixing)
                         withObject:nil
                      waitUntilDone:NO];
}

/*****************************************************************************/
/*                        kdms_window:releaseScrollers                       */
/*****************************************************************************/

- (void)releaseScrollers:(BOOL)main_thread
{
  if (!scrollers_fixed)
    return; // Nothing to do
  if (main_thread)
    { 
      scrollers_fixed = false;
      hor_scroller_fixed_pos = 0.0;
      hor_scroller_fixed_size = 0.0;
      vert_scroller_fixed_pos = 0.0;
      vert_scroller_fixed_size = 0.0;
      [hor_scroller unfix];
      [vert_scroller unfix];
    }
  else
    { 
      scroller_mutex.lock();
      scrollers_fixed = false;
      hor_scroller_fixed_pos = 0.0;
      hor_scroller_fixed_size = 0.0;
      vert_scroller_fixed_pos = 0.0;
      vert_scroller_fixed_size = 0.0;
      scroller_mutex.unlock();
      [self performSelectorOnMainThread:@selector(applyScrollerFixing)
                             withObject:nil
                          waitUntilDone:NO];      
    }
}

/*****************************************************************************/
/*                      kdms_window:applyScrollerFixing                      */
/*****************************************************************************/

- (void)applyScrollerFixing
{
  bool fix;
  double hor_pos, vert_pos, hor_size, vert_size;
  scroller_mutex.lock();
  fix = scrollers_fixed;
  hor_pos = hor_scroller_fixed_pos;
  vert_pos = vert_scroller_fixed_pos;
  hor_size = hor_scroller_fixed_size;
  vert_size = vert_scroller_fixed_size;
  scroller_mutex.unlock();
  if (!fix)
    { 
      [hor_scroller unfix];
      [vert_scroller unfix];
    }
  else
    { 
      [hor_scroller fixPos:hor_pos andSize:hor_size];
      [vert_scroller fixPos:vert_pos andSize:vert_size];
    }
}

/*****************************************************************************/
/*                         kdms_window:do_processing                         */
/*****************************************************************************/

- (bool)do_processing
{
  if (renderer == NULL)
    return false;
  else
    return renderer->do_processing();
}

/*****************************************************************************/
/*                    kdms_window:display_notification                       */
/*****************************************************************************/

- (bool) display_notification
{
  if (renderer == NULL)
    return false;
  renderer->update_animation_status_info();
  renderer->manage_animation_frame_queue();
  return renderer->need_processing();
}

/*****************************************************************************/
/*                     kdms_window:client_notification                       */
/*****************************************************************************/

- (bool)client_notification
{
  if (renderer == NULL)
    return false;
  renderer->client_notification();
  return renderer->need_processing();
}

/*****************************************************************************/
/*                       kdms_window:adjust_playclock                        */
/*****************************************************************************/

- (void)adjust_playclock:(double)delta
{
  if (renderer != NULL)
    renderer->adjust_playclock(delta);
}

/*****************************************************************************/
/*                 kdms_window:wakeupScheduledFor:occurredAt                 */
/*****************************************************************************/

- (void)wakeupScheduledFor:(CFAbsoluteTime)scheduled_time
                occurredAt:(CFAbsoluteTime)current_time
{
  if (renderer != NULL)
    renderer->wakeup(scheduled_time,current_time);
}

/*****************************************************************************/
/*                     kdms_window:cancel_pending_show                       */
/*****************************************************************************/

- (void)cancel_pending_show
{
  if (renderer != NULL)
    renderer->cancel_pending_show();
}

/*****************************************************************************/
/*                        kdms_window:create_glview                          */
/*****************************************************************************/

- (kdms_glview *)create_glview
{
  if (!glview)
    { 
      NSRect content_frame = [[scroll_view contentView] frame];
      glview =
        [[kdms_glview alloc] initWithFrame:content_frame
                                scrollView:scroll_view
                                   docView:doc_view
                               andRenderer:renderer];
      [glview prepareOpenGL];
      [scroll_view addSubview:glview];
      [glview release]; // Retained now by the `scroll_view' object
    }
  return glview;
}

/*****************************************************************************/
/*                       kdms_window:release_glview                          */
/*****************************************************************************/

- (void)release_glview
{
  if (glview)
    [glview removeFromSuperview];
  glview = nil;
}

/*****************************************************************************/
/*             kdms_window:set_min_max_size_for_scroll_view                  */
/*****************************************************************************/

- (void)set_min_max_size_for_scroll_view:(NSSize)max_scroll_view_size
{
  NSRect min_rect;
  min_rect.origin.x = min_rect.origin.y = 0.0F;
  min_rect.size.height = [self get_bottom_margin_height] + 40.0F;
  min_rect.size.width = [self get_right_margin_width] + 40.0F;
  min_rect = [self frameRectForContentRect:min_rect];
  [self setMinSize:min_rect.size];
  
  NSRect content_rect;
  content_rect.origin.x = content_rect.origin.y = 0.0F;
  content_rect.size = max_scroll_view_size;
  content_rect.size.height += [self get_bottom_margin_height];
  content_rect.size.width += [self get_right_margin_width];
  NSRect window_rect = [self frameRectForContentRect:content_rect];
  [self setMaxSize:window_rect.size];  
}

/*****************************************************************************/
/*                kdms_window:changed_bottom_margin_height                   */
/*****************************************************************************/

- (void) changed_bottom_margin_height:(float)old_height;
{
  if (renderer == NULL)
    return; // Don't bother making any adjustments
  float increment = [self get_bottom_margin_height] - old_height;
  
  NSSize doc_size = [doc_view frame].size;
  NSSize max_scroll_view_size =
    [NSScrollView frameSizeForContentSize:doc_size
                  horizontalScrollerClass:[kdms_scroller class]
                    verticalScrollerClass:[kdms_scroller class]
                               borderType:NSNoBorder
                              controlSize:NSRegularControlSize
                            scrollerStyle:NSScrollerStyleLegacy];
  [self set_min_max_size_for_scroll_view:max_scroll_view_size];
  NSSize max_window_size = [self maxSize];
  
  NSRect frame_rect = [self frame];
  frame_rect.size.height += increment;
  if (frame_rect.size.height > max_window_size.height)
    frame_rect.size.height = max_window_size.height;
  [self setFrame:frame_rect display:NO];
  
  // Find the bounding rectangles for the top-level views
  NSView *super_view = [self contentView];
  NSRect content_rect = [super_view frame];
  
  NSRect scroll_view_rect = content_rect;
  scroll_view_rect.origin.y = [self get_bottom_margin_height];
  scroll_view_rect.size.height -= scroll_view_rect.origin.y;
  scroll_view_rect.size.width -= [self get_right_margin_width];
  [scroll_view setFrame:scroll_view_rect];
  
  NSRect status_bar_rect = scroll_view_rect;
  status_bar_rect.origin.y = animation_height;
  status_bar_rect.size.height = status_height;
  [status_bar setFrame:status_bar_rect];
  
  if (animation_bar && (animation_height > 0.0F))
    { 
      NSRect animation_bar_rect = scroll_view_rect;
      animation_bar_rect.origin.y = 0.0F;
      animation_bar_rect.size.height = animation_height;
      [animation_bar setFrame:animation_bar_rect];
    }
  
  if (catalog_panel != nil)
    { 
      NSRect catalog_rect = content_rect;
      catalog_rect.origin.y += 3;
      catalog_rect.size.height -= 3;
      catalog_rect.origin.x = scroll_view_rect.size.width+3;
      catalog_rect.size.width -= catalog_rect.origin.x+3;
      NSRect catalog_panel_rect = catalog_rect;
      catalog_panel_rect.size.height -= catalog_tools_height;
      [catalog_panel setFrame:catalog_panel_rect];
      NSRect catalog_tools_rect = catalog_rect;
      catalog_tools_rect.size.height = catalog_tools_height;
      catalog_tools_rect.origin.y +=
        catalog_rect.size.height - catalog_tools_height;
      [catalog_tools setFrame:catalog_tools_rect];
    }
}

/*****************************************************************************/
/*                 kdms_window:changed_right_margin_width                    */
/*****************************************************************************/

- (void)changed_right_margin_width:(float)old_width;
{
  if (renderer == NULL)
    return; // Don't bother making any adjustments
  
  float increment = [self get_right_margin_width] - old_width;
  NSSize doc_size = [doc_view frame].size;
  NSSize max_scroll_view_size =
    [NSScrollView frameSizeForContentSize:doc_size
                  horizontalScrollerClass:[kdms_scroller class]
                    verticalScrollerClass:[kdms_scroller class]
                               borderType:NSNoBorder
                              controlSize:NSRegularControlSize
                            scrollerStyle:NSScrollerStyleLegacy];
  [self set_min_max_size_for_scroll_view:max_scroll_view_size];
  
  NSRect frame_rect = [self frame];
  frame_rect.size.width += increment;
  [self setFrame:frame_rect display:NO];
  
  // Find the bounding rectangles for the top-level views
  NSView *super_view = [self contentView];
  NSRect content_rect = [super_view frame];
  
  NSRect scroll_view_rect = content_rect;
  scroll_view_rect.origin.y = [self get_bottom_margin_height];
  scroll_view_rect.size.height -= scroll_view_rect.origin.y;
  scroll_view_rect.size.width -= [self get_right_margin_width];
  [scroll_view setFrame:scroll_view_rect];
  
  NSRect status_bar_rect = scroll_view_rect;
  status_bar_rect.origin.y = animation_height;
  status_bar_rect.size.height = status_height;
  [status_bar setFrame:status_bar_rect];
  
  if (animation_bar && (animation_height > 0.0F))
    { 
      NSRect animation_bar_rect = scroll_view_rect;
      animation_bar_rect.origin.y = 0.0F;
      animation_bar_rect.size.height = animation_height;
      [animation_bar setFrame:animation_bar_rect];
    }
  
  if (catalog_panel != nil)
    { 
      NSRect catalog_rect = content_rect;
      catalog_rect.origin.y += 3;
      catalog_rect.size.height -= 3;
      catalog_rect.origin.x = scroll_view_rect.size.width+3;
      catalog_rect.size.width -= catalog_rect.origin.x+3;
      NSRect catalog_panel_rect = catalog_rect;
      catalog_panel_rect.size.height -= catalog_tools_height;
      [catalog_panel setFrame:catalog_panel_rect];
      NSRect catalog_tools_rect = catalog_rect;
      catalog_tools_rect.size.height = catalog_tools_height;
      catalog_tools_rect.origin.y +=
        catalog_rect.size.height - catalog_tools_height;
      [catalog_tools setFrame:catalog_tools_rect];
    }
}

/*****************************************************************************/
/*                    kdms_window:create_metadata_catalog                    */
/*****************************************************************************/

- (void)create_metadata_catalog
{
  [self remove_metadata_catalog]; // Reset everything, just in case
  if (renderer == NULL)
    return;
  
  // Resize/place existing components
  NSView *super_view = [self contentView];
  [super_view setAutoresizesSubviews:NO];
  float old_margin_width = [self get_right_margin_width];
  catalog_panel_width = 280.0F; // A reasonable panel width
  [self changed_right_margin_width:old_margin_width];

  // Add the catalog
  NSRect content_rect = [super_view frame];
  NSRect catalog_rect = content_rect;
  catalog_rect.origin.x = content_rect.size.width - catalog_panel_width + 3;
  catalog_rect.size.width = catalog_panel_width - 6;
  catalog_rect.origin.y += 3;
  catalog_rect.size.height -= 3;

  // Start by creating the catalog tools box
  catalog_tools_height = 3*status_pane_height; // Rough guess; correct later
  NSRect tools_rect = catalog_rect;
  tools_rect.size.height = catalog_tools_height;
  tools_rect.origin.y += catalog_rect.size.height - catalog_tools_height;
  catalog_tools =
    [[NSBox alloc] initWithFrame:tools_rect];
  [catalog_tools setTitlePosition:NSAtTop];
  [catalog_tools setTitle:@"Catalog Pastebar/Tools"];
  [catalog_tools setBorderType:NSLineBorder];
  [catalog_tools setAutoresizingMask:NSViewMinXMargin|NSViewMinYMargin];
  [super_view addSubview:catalog_tools];
  [catalog_tools release];
  NSView *tools_view = [catalog_tools contentView];
  NSRect tools_view_rect = [tools_view frame];
  NSSize tools_margin = [catalog_tools contentViewMargins];
  NSRect tools_place_rect = tools_view_rect;
  tools_place_rect.size.width += tools_margin.width*2.0F - 4;
  tools_place_rect.origin.x = -(tools_margin.width - 2);
  tools_place_rect.size.height += tools_margin.height*2.0F - 4;
  tools_place_rect.origin.y = -tools_margin.height;
  float delta_height = 28+status_height - tools_place_rect.size.height;
  catalog_tools_height += delta_height;
  tools_rect.size.height = catalog_tools_height;
  tools_place_rect.size.height += delta_height;
  tools_rect.origin.y -= delta_height;
  [catalog_tools setFrame:tools_rect];
  
  // Now create the pastebar
  NSColor *catalog_background_colour =
    [NSColor colorWithCalibratedRed:1.0F green:1.0F blue:0.95F alpha:1.0F];
  NSRect pastebar_rect;
  pastebar_rect = tools_place_rect;
  pastebar_rect.origin.y += 26;
  pastebar_rect.size.height = status_height;
  pastebar_rect.size.width -= 4.0;
  pastebar_rect.origin.x += 4.0;
  
  NSRect clear_rect = pastebar_rect;
  clear_rect.size.width = pastebar_rect.size.height;
  clear_rect.origin.x = pastebar_rect.size.width - clear_rect.size.width-2.0;
  pastebar_rect.size.width -= clear_rect.size.width + 5.0;
  catalog_paste_clear =
    [[NSButton alloc] initWithFrame:clear_rect];
  [catalog_paste_clear setButtonType:NSMomentaryPushInButton];
  [catalog_paste_clear setTitle:@"x"];
  [catalog_paste_clear setBezelStyle:NSCircularBezelStyle];
  [catalog_paste_clear setEnabled:NO];
  [catalog_paste_clear setBordered:YES];
  [tools_view addSubview:catalog_paste_clear];
  [catalog_paste_clear release];
  
  catalog_paste_bar =
    [[NSButton alloc] initWithFrame:pastebar_rect];
  [catalog_paste_bar setButtonType:NSMomentaryLightButton];
  [catalog_paste_bar setBezelStyle:NSRoundRectBezelStyle];
  [catalog_paste_bar setEnabled:NO];
  [catalog_paste_bar setTitle:@""];
  [catalog_paste_bar setAutoresizingMask:NSViewWidthSizable];
  [catalog_paste_bar setBordered:YES];
  [catalog_paste_bar setShowsBorderOnlyWhileMouseInside:YES];
  [[catalog_paste_bar cell] setFont:status_font];
  [tools_view addSubview:catalog_paste_bar];
  [catalog_paste_bar release];
  
  // Now create the buttons
  int b;
  unichar arrows[3] = {0x21E6,0x21E8,0x27F3};
  NSFont *arrow_font =
    [NSFont boldSystemFontOfSize:
     2.0F*[NSFont systemFontSizeForControlSize:NSRegularControlSize]];
  for (b=0; b < 3; b++)
    {
      NSRect button_rect = tools_place_rect;
      button_rect.origin.y += 4;
      button_rect.size.height = 19;
      button_rect.size.width = tools_place_rect.size.width * 0.3F;
      if (b == 0)
        button_rect.origin.x += 4;
      if (b == 1)
        button_rect.origin.x +=
          (tools_place_rect.size.width - button_rect.size.width) * 0.5F;
      else if (b == 2)
        button_rect.origin.x +=
          tools_place_rect.size.width - button_rect.size.width-4;
      catalog_buttons[b] =
        [[NSButton alloc] initWithFrame:button_rect];
      if (b == 2)
        [catalog_buttons[b] setTitle:@"peers"];
      else
        {
          [[catalog_buttons[b] cell] setFont:arrow_font];
          [catalog_buttons[b] setTitle:[NSString stringWithCharacters:arrows+b
                                                               length:1]];
        }
      [catalog_buttons[b] setButtonType:NSMomentaryLightButton];
      [catalog_buttons[b] setBordered:YES];
      [catalog_buttons[b] setBezelStyle:NSRoundRectBezelStyle];
      [catalog_buttons[b] setEnabled:NO];
      [catalog_buttons[b] setAutoresizingMask:
       NSViewMaxXMargin|NSViewMinXMargin|NSViewWidthSizable];
      [tools_view addSubview:catalog_buttons[b]];
    }

  // Now create the catalog panel and view
  NSRect catalog_panel_rect = catalog_rect;
  catalog_panel_rect.size.height -= tools_rect.size.height;
  catalog_panel =
    [[NSScrollView alloc] initWithFrame:catalog_panel_rect];
  [catalog_panel setBorderType:NSBezelBorder];
  [catalog_panel setAutoresizingMask:
   NSViewHeightSizable|NSViewMinXMargin];
  [catalog_panel setHasVerticalScroller:YES];
  [catalog_panel setHasHorizontalScroller:YES];
  [super_view addSubview:catalog_panel];
  [catalog_panel release];
  
  kdms_catalog *catalog_view =
    [[kdms_catalog alloc] initWithFrame:[[catalog_panel contentView] frame]
                               pasteBar:catalog_paste_bar
                             pasteClear:catalog_paste_clear
                             backButton:catalog_buttons[0]
                              fwdButton:catalog_buttons[1]
                             peerButton:catalog_buttons[2]
                            andRenderer:renderer];
  [catalog_view setBackgroundColor:catalog_background_colour];
  [catalog_panel setDocumentView:catalog_view];
  [catalog_view release]; // Retained by catalog_panel

  // Finalize the configuration
  [super_view setAutoresizesSubviews:YES];
  renderer->set_metadata_catalog_source(catalog_view);
}

/*****************************************************************************/
/*                    kdms_window:remove_metadata_catalog                    */
/*****************************************************************************/

- (void)remove_metadata_catalog
{
  NSView *super_view = [self contentView];
  [super_view setAutoresizesSubviews:NO];
  float old_margin_width = [self get_right_margin_width];
  if (catalog_panel)
    { 
      [catalog_tools removeFromSuperview];
      [catalog_panel removeFromSuperview];
      catalog_panel = nil; // Should have been auto-released by above function
      catalog_tools = nil; // Should have been auto-released by above function
      catalog_paste_bar = nil;
      catalog_paste_clear = nil;
      catalog_buttons[0] = catalog_buttons[1] = catalog_buttons[2] = nil;
      catalog_panel_width = 0.0F;
    }
  catalog_panel_width = 0.0F; // A reasonable panel width
  [self changed_right_margin_width:old_margin_width];
  [super_view setAutoresizesSubviews:YES];
  if (renderer != NULL)
    renderer->set_metadata_catalog_source(NULL);
}

/*****************************************************************************/
/*                       kdms_window:set_progress_bar                        */
/*****************************************************************************/

- (void)set_progress_bar:(double)value
{
  if (progress_indicator == nil)
    { 
      assert(progress_height == 0.0F);
      NSView *super_view = [self contentView];
      [super_view setAutoresizesSubviews:NO];      
      float old_height = [self get_bottom_margin_height];
      progress_height = status_pane_height * 0.5F;
      status_height += progress_height;
      [self changed_bottom_margin_height:old_height];
    
      // Add in the progress indicator
      NSRect progress_rect = [status_bar frame];
      progress_rect.origin.x = 0.0F;
      progress_rect.origin.y = status_pane_height;
      progress_rect.size.height = progress_height;
      progress_indicator =
        [[NSProgressIndicator alloc] initWithFrame:progress_rect];
      [progress_indicator setIndeterminate:NO];
      [progress_indicator
       setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin |
                            NSViewMinXMargin | NSViewMaxXMargin)];
      [status_bar addSubview:progress_indicator];
      [progress_indicator release]; // Retained by the status bar
      
      // Re-activate auto-resizing
      [super_view setAutoresizesSubviews:YES];
    }
  [progress_indicator setDoubleValue:value];
}

/*****************************************************************************/
/*                      kdms_window:remove_progress_bar                      */
/*****************************************************************************/

- (void)remove_progress_bar
{
  if (!progress_indicator)
    return;
  
  NSView *super_view = [self contentView];
  [super_view setAutoresizesSubviews:NO];
  float old_height = [self get_bottom_margin_height];
  [progress_indicator removeFromSuperview];
  progress_indicator = nil;
  status_height = status_pane_height;
  progress_height = 0.0F;
  [self changed_bottom_margin_height:old_height];
  [super_view setAutoresizesSubviews:YES];
}

/*****************************************************************************/
/*                       kdms_window:get_animation_bar                       */
/*****************************************************************************/

- (kdms_animation_bar *)get_animation_bar:(bool)allow_positioning
{
  if (!animation_bar)
    return nil;
  
  if (animation_height == 0.0F)
    { 
      NSView *super_view = [self contentView];
      [super_view setAutoresizesSubviews:NO];      
      float old_height = [self get_bottom_margin_height];
      animation_height = 2.0F*animation_line_height + 8.0F;
      [animation_bar setAutoresizingMask:
                   (NSViewWidthSizable | NSViewMinXMargin)];
      [super_view addSubview:animation_bar];
      [self changed_bottom_margin_height:old_height];
      [super_view setAutoresizesSubviews:YES];
    }
  [animation_bar set_target:renderer can_position:allow_positioning];
  return animation_bar;
}

/*****************************************************************************/
/*                      kdms_window:remove_animation_bar                     */
/*****************************************************************************/

- (void)remove_animation_bar
{
  if ((animation_height == 0.0F) || !animation_bar)
    return;
  [animation_bar set_target:NULL can_position:true];
  
  NSView *super_view = [self contentView];
  [super_view setAutoresizesSubviews:NO];
  float old_height = [self get_bottom_margin_height];
  [animation_bar removeFromSuperview];
  animation_height = 0.0F;
  [self changed_bottom_margin_height:old_height];
  [super_view setAutoresizesSubviews:YES];
}

/*****************************************************************************/
/*                       kdms_window:reset_animation_bar                     */
/*****************************************************************************/

- (void)reset_animation_bar
{
  if (animation_bar)
    [animation_bar reset];
}

/*****************************************************************************/
/*                    kdms_window:get_bottom_margin_height                   */
/*****************************************************************************/

- (float)get_bottom_margin_height
{
  return status_height + animation_height;
}

/*****************************************************************************/
/*                    kdms_window:get_right_margin_width                     */
/*****************************************************************************/

- (float)get_right_margin_width
{
  return catalog_panel_width; 
}

/*****************************************************************************/
/*                       kdms_window:set_status_strings                      */
/*****************************************************************************/

- (void)set_status_strings:(const char **)three_strings
{
  int p; // Panel index
  
  // Start by finding the status panes which have changed, and allocating
  // space to cache the new status strings.
  int total_chars = 0; // Including NULL chars.
  bool pane_updated[3] = {false,false,false};
  for (p=0; p < 3; p++)
    {
      const char *string = three_strings[p];
      if (string == NULL)
        string = "";
      int len = (int)strlen(string)+1;
      pane_updated[p] = ((len != status_pane_lengths[p]) ||
                         (strcmp(string,status_pane_caches[p]) != 0));
      if ((total_chars + len) <= 256)
        { // Can cache this string
          status_pane_lengths[p] = len;
          status_pane_caches[p] = status_pane_cache_buf + total_chars;
          total_chars += len;
        }
      else
        { // Cannot cache this string
          status_pane_lengths[p] = 0;
          status_pane_caches[p] = NULL;
        }          
    }
  
  // Now update any status panes whose text has changed and also cache the
  // new text if we can, for next time.  Caching allows us to avoid updating
  // too many status panes, which can speed things up in video applications,
  // for example.
  for (p=0; p < 3; p++)
    { 
      if (!pane_updated[p])
        continue;
      const char *string = three_strings[p];
      if (string == NULL)
        string = "";
      if (status_pane_caches[p] != NULL)
        strcpy(status_pane_caches[p],string);
      NSString *ns_string = [NSString stringWithUTF8String:string];
      NSTextFieldCell *cell = [status_panes[p] cell];
      [cell setTitle:ns_string];
    }
}

/*****************************************************************************/
/*                        kdms_window:set_drag_cursor                        */
/*****************************************************************************/

- (void)set_drag_cursor:(BOOL)is_dragging
{
  if (!scroll_view)
    return;
  if (is_dragging)
    [scroll_view setDocumentCursor:doc_cursor_dragging];
  else
    [scroll_view setDocumentCursor:doc_cursor_normal];
}

/*****************************************************************************/
/*                          kdms_window:menuFile....                         */
/*****************************************************************************/

- (IBAction) menuFileOpen:(NSMenuItem *)sender
{
  NSArray *file_types =
    [NSArray arrayWithObjects:
     @"jp2",@"jpx",@"jpf",@"mj2",@"j2c",@"j2k",@"kjc",nil];
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  [panel setPrompt:@"Open"];
  [panel setMessage:@"Click \"Cancel\" to leave everything as-is"];
  [panel setTitle:@"Choose file(s) to open"];
  [panel setAllowsMultipleSelection:YES];
  [panel setAllowedFileTypes:file_types];
  if ([panel runModal] == NSModalResponseOK)
    { // Open one or more files
      NSArray *urls = [panel URLs];
      for (int n=0; n < urls.count; n++)
        {
          NSString *filename = [[urls objectAtIndex:n] path];
          if (n == 0)
            [self open_file:filename];
          else
            {
              kdms_window *wnd =
                [[kdms_window alloc] initWithManager:window_manager
                                          andCursors:manager_cursors];
              [wnd open_file:filename];
            }
        }
    }
}

- (IBAction) menuFileOpenUrl:(NSMenuItem *)sender
{
  kdms_url_dialog *dialog = [[kdms_url_dialog alloc] init];
  [dialog setTitle:@"Open JPIP URL in Current Window"];
  NSString *url = [dialog run_modal];
  [dialog release];
  if (url != nil)
    [self open_url:url];
}

- (IBAction) menuFileDisconnectURL:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileDisconnectURL();
  [window_manager->get_next_action_window(self) menuFileDisconnectURL:sender];
}

- (IBAction) menuFileReconnectURL:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileReconnectURL();
}

- (IBAction) menuFileEraseAndReconnectURL:(NSMenuItem *)sender
{ 
  if (renderer != NULL) renderer->menu_FileEraseAndReconnectURL();
}

- (IBAction) menuFileJPIPSaveMode:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileJPIPSaveMode();
}

- (IBAction) menuFileJPIPUnsavedMode:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileJPIPUnsavedMode();
}

- (IBAction) menuFileSave:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileSave();
  [window_manager->get_next_action_window(self) menuFileSave:sender];
}

- (IBAction) menuFileSaveAs:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileSaveAs(true,false);
}

- (IBAction) menuFileSaveAsLinked:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileSaveAs(true,true);
}

- (IBAction) menuFileSaveAsEmbedded:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileSaveAs(false,false);
}

- (IBAction) menuFileSaveReload:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileSaveReload();
  [window_manager->get_next_action_window(self) menuFileSaveReload:sender];
}

- (IBAction) menuFileProperties:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FileProperties();
}

/*****************************************************************************/
/*                          kdms_window:menuView....                         */
/*****************************************************************************/

- (IBAction) menuViewHflip:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewHflip();
  [window_manager->get_next_action_window(self) menuViewHflip:sender];
}

- (IBAction) menuViewVflip:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewVflip();
  [window_manager->get_next_action_window(self) menuViewVflip:sender];
}

- (IBAction) menuViewRotate:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewRotate();
  [window_manager->get_next_action_window(self) menuViewRotate:sender];
}

- (IBAction) menuViewCounterRotate:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewCounterRotate();
  [window_manager->get_next_action_window(self) menuViewCounterRotate:sender];
}

- (IBAction) menuViewZoomIn:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewZoomIn();
  [window_manager->get_next_action_window(self) menuViewZoomIn:sender];
}

- (IBAction) menuViewZoomOut:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewZoomOut();
  [window_manager->get_next_action_window(self) menuViewZoomOut:sender];
}

- (IBAction) menuViewZoomInSlightly:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewZoomInSlightly();
  [window_manager->get_next_action_window(self) menuViewZoomInSlightly:sender];
}

- (IBAction) menuViewZoomOutSlightly:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewZoomOutSlightly();
  [window_manager->get_next_action_window(self) menuViewZoomOutSlightly:sender];
}

- (IBAction) menuViewWiden:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewWiden();
  [window_manager->get_next_action_window(self) menuViewWiden:sender];
}

- (IBAction) menuViewShrink:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewShrink();
  [window_manager->get_next_action_window(self) menuViewShrink:sender];
}

- (IBAction) menuViewRestore:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewRestore();
  [window_manager->get_next_action_window(self) menuViewRestore:sender];
}

- (IBAction) menuViewRefresh:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewRefresh();
  [window_manager->get_next_action_window(self) menuViewRefresh:sender];
}

- (IBAction) menuViewLayersLess:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewLayersLess();
  [window_manager->get_next_action_window(self) menuViewLayersLess:sender];
}

- (IBAction) menuViewLayersMore:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewLayersMore();
  [window_manager->get_next_action_window(self) menuViewLayersMore:sender];
}

- (IBAction) menuViewOptimizeScale:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewOptimizeScale();
  [window_manager->get_next_action_window(self) menuViewOptimizeScale:sender];
}

- (IBAction) menuViewPixelScaleX1:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewPixelScaleX1();
  [window_manager->get_next_action_window(self) menuViewPixelScaleX1:sender];
}

- (IBAction) menuViewPixelScaleX2:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ViewPixelScaleX2();
  [window_manager->get_next_action_window(self) menuViewPixelScaleX2:sender];
}

/*****************************************************************************/
/*                         kdms_window:menuFocus....                         */
/*****************************************************************************/

- (IBAction) menuFocusOff:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FocusOff();
  [window_manager->get_next_action_window(self) menuFocusOff:sender];
}

- (IBAction) menuFocusHighlighting:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FocusHighlighting();
  [window_manager->get_next_action_window(self) menuFocusHighlighting:sender];
}

- (IBAction) menuFocusWiden:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FocusWiden();
  [window_manager->get_next_action_window(self) menuFocusWiden:sender];
}

- (IBAction) menuFocusShrink:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FocusShrink();
  [window_manager->get_next_action_window(self) menuFocusShrink:sender];
}

- (IBAction) menuFocusLeft:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FocusLeft();
  [window_manager->get_next_action_window(self) menuFocusLeft:sender];
}

- (IBAction) menuFocusRight:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FocusRight();
  [window_manager->get_next_action_window(self) menuFocusRight:sender];
}

- (IBAction) menuFocusUp:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FocusUp();
  [window_manager->get_next_action_window(self) menuFocusUp:sender];
}

- (IBAction) menuFocusDown:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_FocusDown();
  [window_manager->get_next_action_window(self) menuFocusDown:sender];
}

/*****************************************************************************/
/*                          kdms_window:menuMode....                         */
/*****************************************************************************/

- (IBAction) menuModeFast:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ModeFast();
  [window_manager->get_next_action_window(self) menuModeFast:sender];
}

- (IBAction) menuModeFussy:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ModeFussy();
  [window_manager->get_next_action_window(self) menuModeFussy:sender];
}

- (IBAction) menuModeResilient:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ModeResilient();
  [window_manager->get_next_action_window(self) menuModeResilient:sender];
}

- (IBAction) menuModeResilientSOP:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ModeResilientSOP();
  [window_manager->get_next_action_window(self) menuModeResilientSOP:sender];
}

- (IBAction) menuModeSingleThreaded:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ModeSingleThreaded();
  [window_manager->get_next_action_window(self) menuModeSingleThreaded:sender];
}

- (IBAction) menuModeMultiThreaded:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ModeMultiThreaded();
  [window_manager->get_next_action_window(self) menuModeMultiThreaded:sender];
}

- (IBAction) menuModeMoreThreads:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ModeMoreThreads();
  [window_manager->get_next_action_window(self) menuModeMoreThreads:sender];
}

- (IBAction) menuModeLessThreads:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ModeLessThreads();
  [window_manager->get_next_action_window(self) menuModeLessThreads:sender];
}

- (IBAction) menuModeRecommendedThreads:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_ModeRecommendedThreads();
  [window_manager->get_next_action_window(self)
   menuModeRecommendedThreads:sender];
}

/*****************************************************************************/
/*                          kdms_window:menuNav....                          */
/*****************************************************************************/

- (IBAction) menuNavComponent1:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_NavComponent1();
  [window_manager->get_next_action_window(self) menuNavComponent1:sender];
}

- (IBAction) menuNavMultiComponent:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_NavMultiComponent();
  [window_manager->get_next_action_window(self) menuNavMultiComponent:sender];
}

- (IBAction) menuNavComponentNext:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_NavComponentNext();
  [window_manager->get_next_action_window(self) menuNavComponentNext:sender];
}

- (IBAction) menuNavComponentPrev:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_NavComponentPrev();
  [window_manager->get_next_action_window(self) menuNavComponentPrev:sender];
}

- (IBAction) menuNavImageNext:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_NavImageNext();
  [window_manager->get_next_action_window(self) menuNavImageNext:sender];
}

- (IBAction) menuNavImagePrev:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_NavImagePrev();
  [window_manager->get_next_action_window(self) menuNavImagePrev:sender];
}

- (IBAction) menuNavCompositingLayer:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_NavCompositingLayer();
  [window_manager->get_next_action_window(self)
   menuNavCompositingLayer:sender];
}

- (IBAction) menuNavTrackNext:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_NavTrackNext();
  [window_manager->get_next_action_window(self) menuNavTrackNext:sender];
}

/*****************************************************************************/
/*                        kdms_window:menuMetadata...                        */
/*****************************************************************************/

- (IBAction) menuMetadataCatalog:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataCatalog();
  [window_manager->get_next_action_window(self) menuMetadataCatalog:sender];
}

- (IBAction) menuMetadataSwapFocus:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataSwapFocus();
  [window_manager->get_next_action_window(self) menuMetadataSwapFocus:sender];
}

- (IBAction) menuMetadataShow:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataShow();
  [window_manager->get_next_action_window(self) menuMetadataShow:sender];
}

- (IBAction) menuMetadataAdd:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataAdd();
}

- (IBAction) menuMetadataEdit:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataEdit();
}

- (IBAction) menuMetadataDelete:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataDelete();
}

- (IBAction) menuMetadataCopyLabel:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataCopyLabel();  
}

- (IBAction) menuMetadataCopyLink:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataCopyLink();
}

- (IBAction) menuMetadataCut:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataCut();
}

- (IBAction) menuMetadataPasteNew:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataPasteNew();
}

- (IBAction) menuMetadataDuplicate:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataDuplicate();
}

- (IBAction) menuMetadataUndo:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataUndo();
}

- (IBAction) menuMetadataRedo:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_MetadataRedo();
}

- (IBAction) menuOverlayEnable:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_OverlayEnable();
  [window_manager->get_next_action_window(self) menuOverlayEnable:sender];
}

- (IBAction) menuOverlayFlashing:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_OverlayFlashing();
  [window_manager->get_next_action_window(self) menuOverlayFlashing:sender];
}

- (IBAction) menuOverlayAutoFlash:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_OverlayAutoFlash();
  [window_manager->get_next_action_window(self) menuOverlayAutoFlash:sender];
}

- (IBAction) menuOverlayToggle:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_OverlayToggle();
  [window_manager->get_next_action_window(self) menuOverlayToggle:sender];
}

- (IBAction) menuOverlayRestrict:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_OverlayRestrict();
}

- (IBAction) menuOverlayLighter:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_OverlayLighter();
  [window_manager->get_next_action_window(self) menuOverlayLighter:sender];
}

- (IBAction) menuOverlayHeavier:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_OverlayHeavier();
  [window_manager->get_next_action_window(self) menuOverlayHeavier:sender];
}

- (IBAction) menuOverlayDoubleSize:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_OverlayDoubleSize();
  [window_manager->get_next_action_window(self) menuOverlayDoubleSize:sender];
}

- (IBAction) menuOverlayHalveSize:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_OverlayHalveSize();
  [window_manager->get_next_action_window(self) menuOverlayHalveSize:sender];
}

/*****************************************************************************/
/*                          kdms_window:menuPlay....                         */
/*****************************************************************************/

- (IBAction) menuPlayStartForward:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_PlayStartForward();
  [window_manager->get_next_action_window(self) menuPlayStartForward:sender];
}

- (IBAction) menuPlayStartBackward:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_PlayStartBackward();
  [window_manager->get_next_action_window(self) menuPlayStartBackward:sender];
}

- (IBAction) menuPlayStop:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_PlayStop();
  [window_manager->get_next_action_window(self) menuPlayStop:sender];
}

- (IBAction) menuPlayRenderAll:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_PlayRenderAll();
  [window_manager->get_next_action_window(self) menuPlayRenderAll:sender];
}

- (IBAction) menuPlayRepeat:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_PlayRepeat();
  [window_manager->get_next_action_window(self) menuPlayRepeat:sender];
}

- (IBAction) menuPlayNative:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_PlayNative();
  [window_manager->get_next_action_window(self) menuPlayNative:sender];
}

- (IBAction) menuPlayCustom:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_PlayCustom();
  [window_manager->get_next_action_window(self) menuPlayCustom:sender];
}

- (IBAction) menuPlayFrameRateUp:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_PlayFrameRateUp();
  [window_manager->get_next_action_window(self) menuPlayFrameRateUp:sender];
}

- (IBAction) menuPlayFrameRateDown:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_PlayFrameRateDown();
  [window_manager->get_next_action_window(self) menuPlayFrameRateDown:sender];
}

/*****************************************************************************/
/*                         kdms_window:menuStatus....                        */
/*****************************************************************************/

- (IBAction) menuStatusToggle:(NSMenuItem *)sender
{
  if (renderer != NULL) renderer->menu_StatusToggle();
  [window_manager->get_next_action_window(self) menuStatusToggle:sender];
}

/*****************************************************************************/
/*                         kdms_window:menuWindow....                        */
/*****************************************************************************/

- (IBAction) menuWindowDuplicate:(NSMenuItem *)sender
{
  if ((renderer != NULL) && renderer->can_do_WindowDuplicate(nil))
    {
      kdms_window *wnd = 
        [[kdms_window alloc] initWithManager:window_manager
                                  andCursors:manager_cursors];
      wnd->renderer->open_as_duplicate(self->renderer);
    }
}

- (IBAction) menuWindowNext:(NSMenuItem *)sender
{
  int idx = window_manager->get_access_idx(self);
  if (idx < 0) return;
  kdms_window *target_wnd = window_manager->access_window(idx+1);
  if (target_wnd == nil)
    target_wnd = window_manager->access_window(0);
  if (target_wnd == nil)
    return;
  [target_wnd makeKeyAndOrderFront:self];
}

- (bool) can_do_WindowNext
{
  int idx = window_manager->get_access_idx(self);
  return ((idx > 0) || ((idx == 0) && window_manager->access_window(idx+1)));
}

/*****************************************************************************/
/*                        kdms_window:validateMenuItem                       */
/*****************************************************************************/

- (BOOL) validateMenuItem:(NSMenuItem *)item
{
  bool item_recognized = true;
  bool validated = true;
  
  if (renderer == NULL)
    item_recognized = false;
  //-----------------------------------------------------------
  else if ([item action] == @selector(menuFileDisconnectURL:))
    validated = renderer->can_do_FileDisconnectURL(item);
  else if ([item action] == @selector(menuFileReconnectURL:))
    validated = renderer->can_do_FileReconnectURL(item);
  else if ([item action] == @selector(menuFileEraseAndReconnectURL:))
    validated = renderer->can_do_FileEraseAndReconnectURL(item);
  else if ([item action] == @selector(menuFileJPIPSaveMode:))
    validated = renderer->can_do_FileJPIPSaveMode(item);
  else if ([item action] == @selector(menuFileJPIPUnsavedMode:))
    validated = renderer->can_do_FileJPIPUnsavedMode(item);
  else if ([item action] == @selector(menuFileSave:))
    validated = renderer->can_do_FileSave(item);
  else if ([item action] == @selector(menuFileSaveAs:))
    validated = renderer->can_do_FileSaveAs(item);
  else if ([item action] == @selector(menuFileSaveAsLinked:))
    validated = renderer->can_do_FileSaveAs(item);
  else if ([item action] == @selector(menuFileSaveAsEmbedded:))
    validated = renderer->can_do_FileSaveAs(item);
  else if ([item action] == @selector(menuFileSaveReload:))
    validated = renderer->can_do_FileSaveReload(item);
  else if ([item action] == @selector(menuFileProperties:))
    validated = renderer->can_do_FileProperties(item);
  //-----------------------------------------------------------    
  else if ([item action] == @selector(menuViewHflip:))
    validated = renderer->can_do_ViewHflip(item);
  else if ([item action] == @selector(menuViewVflip:))
    validated = renderer->can_do_ViewVflip(item);
  else if ([item action] == @selector(menuViewRotate:))
    validated = renderer->can_do_ViewRotate(item);
  else if ([item action] == @selector(menuViewCounterRotate:))
    validated = renderer->can_do_ViewCounterRotate(item);
  else if ([item action] == @selector(menuViewZoomIn:))
    validated = renderer->can_do_ViewZoomIn(item);
  else if ([item action] == @selector(menuViewZoomOut:))
    validated = renderer->can_do_ViewZoomOut(item);
  else if ([item action] == @selector(menuViewWiden:))
    validated = renderer->can_do_ViewWiden(item);
  else if ([item action] == @selector(menuViewShrink:))
    validated = renderer->can_do_ViewShrink(item);
  else if ([item action] == @selector(menuViewRestore:))
    validated = renderer->can_do_ViewRestore(item);
  else if ([item action] == @selector(menuViewOptimizeScale:))
    validated = renderer->can_do_ViewOptimizeScale(item);
  else if ([item action] == @selector(menuViewLayersLess:))
    validated = renderer->can_do_ViewLayersLess(item);
  else if ([item action] == @selector(menuViewLayersMore:))
    validated = renderer->can_do_ViewLayersMore(item);
  else if ([item action] == @selector(menuViewPixelScaleX1:))
    validated = renderer->can_do_ViewPixelScaleX1(item);
  else if ([item action] == @selector(menuViewPixelScaleX2:))
    validated = renderer->can_do_ViewPixelScaleX2(item);
  //-----------------------------------------------------------
  else if ([item action] == @selector(menuFocusOff:))
    validated = renderer->can_do_FocusOff(item);
  else if ([item action] == @selector(menuFocusHighlighting:))
    validated = renderer->can_do_FocusHighlighting(item);
  else if ([item action] == @selector(menuFocusWiden:))
    validated = renderer->can_do_FocusWiden(item);
  else if ([item action] == @selector(menuFocusShrink:))
    validated = renderer->can_do_FocusShrink(item);
  else if ([item action] == @selector(menuFocusLeft:))
    validated = renderer->can_do_FocusLeft(item);  
  else if ([item action] == @selector(menuFocusRight:))
    validated = renderer->can_do_FocusRight(item);
  else if ([item action] == @selector(menuFocusUp:))
    validated = renderer->can_do_FocusUp(item);  
  else if ([item action] == @selector(menuFocusDown:))
    validated = renderer->can_do_FocusDown(item);
  //-----------------------------------------------------------
  else if ([item action] == @selector(menuModeFast:))
    validated = renderer->can_do_ModeFast(item);
  else if ([item action] == @selector(menuModeFussy:))
    validated = renderer->can_do_ModeFussy(item);
  else if ([item action] == @selector(menuModeResilient:))
    validated = renderer->can_do_ModeResilient(item);
  else if ([item action] == @selector(menuModeResilientSOP:))
    validated = renderer->can_do_ModeResilientSOP(item);
  else if ([item action] == @selector(menuModeSingleThreaded:))
    validated = renderer->can_do_ModeSingleThreaded(item);
  else if ([item action] == @selector(menuModeMultiThreaded:))
    validated = renderer->can_do_ModeMultiThreaded(item);
  else if ([item action] == @selector(menuModeMoreThreads:))
    validated = renderer->can_do_ModeMoreThreads(item);
  else if ([item action] == @selector(menuModeLessThreads:))
    validated = renderer->can_do_ModeLessThreads(item);
  else if ([item action] == @selector(menuModeRecommendedThreads:))
    validated = renderer->can_do_ModeRecommendedThreads(item);
  //-----------------------------------------------------------
  else if ([item action] == @selector(menuNavComponent1:))
    validated = renderer->can_do_NavComponent1(item);
  else if ([item action] == @selector(menuNavMultiComponent:))
    validated = renderer->can_do_NavMultiComponent(item);
  else if ([item action] == @selector(menuNavComponentNext:))
    validated = renderer->can_do_NavComponentNext(item);
  else if ([item action] == @selector(menuNavComponentPrev:))
    validated = renderer->can_do_NavComponentPrev(item);
  else if ([item action] == @selector(menuNavImageNext:))
    validated = renderer->can_do_NavImageNext(item);
  else if ([item action] == @selector(menuNavImagePrev:))
    validated = renderer->can_do_NavImagePrev(item);
  else if ([item action] == @selector(menuNavCompositingLayer:))
    validated = renderer->can_do_NavCompositingLayer(item);
  else if ([item action] == @selector(menuNavTrackNext:))
    validated = renderer->can_do_NavTrackNext(item);
  //-----------------------------------------------------------
  else if ([item action] == @selector(menuMetadataCatalog:))
    validated = renderer->can_do_MetadataCatalog(item);
  else if ([item action] == @selector(menuMetadataSwapFocus:))
    validated = renderer->can_do_MetadataSwapFocus(item);
  else if ([item action] == @selector(menuMetadataShow:))
    validated = renderer->can_do_MetadataShow(item);
  else if ([item action] == @selector(menuMetadataAdd:))
    validated = renderer->can_do_MetadataAdd(item);
  else if ([item action] == @selector(menuMetadataEdit:))
    validated = renderer->can_do_MetadataEdit(item);
  else if ([item action] == @selector(menuMetadataDelete:))
    validated = renderer->can_do_MetadataDelete(item);
  else if ([item action] == @selector(menuMetadataCopyLabel:))
    validated = renderer->can_do_MetadataCopyLabel(item);
  else if ([item action] == @selector(menuMetadataCopyLink:))
    validated = renderer->can_do_MetadataCopyLink(item);
  else if ([item action] == @selector(menuMetadataCut:))
    validated = renderer->can_do_MetadataCut(item);
  else if ([item action] == @selector(menuMetadataPasteNew:))
    validated = renderer->can_do_MetadataPasteNew(item);
  else if ([item action] == @selector(menuMetadataDuplicate:))
    validated = renderer->can_do_MetadataDuplicate(item);
  else if ([item action] == @selector(menuMetadataUndo:))
    validated = renderer->can_do_MetadataUndo(item);
  else if ([item action] == @selector(menuMetadataRedo:))
    validated = renderer->can_do_MetadataRedo(item);  
  else if ([item action] == @selector(menuOverlayEnable:))
    validated = renderer->can_do_OverlayEnable(item);
  else if ([item action] == @selector(menuOverlayFlashing:))
    validated = renderer->can_do_OverlayFlashing(item);
  else if ([item action] == @selector(menuOverlayAutoFlash:))
    validated = renderer->can_do_OverlayAutoFlash(item);
  else if ([item action] == @selector(menuOverlayToggle:))
    validated = renderer->can_do_OverlayToggle(item);
  else if ([item action] == @selector(menuOverlayRestrict:))
    validated = renderer->can_do_OverlayRestrict(item);
  else if ([item action] == @selector(menuOverlayLighter:))
    validated = renderer->can_do_OverlayLighter(item);
  else if ([item action] == @selector(menuOverlayHeavier:))
    validated = renderer->can_do_OverlayHeavier(item);
  else if ([item action] == @selector(menuOverlayDoubleSize:))
    validated = renderer->can_do_OverlayDoubleSize(item);
  else if ([item action] == @selector(menuOverlayHalveSize:))
    validated = renderer->can_do_OverlayHalveSize(item);
  //-----------------------------------------------------------
  else if ([item action] == @selector(menuPlayStartForward:))
    validated = renderer->can_do_PlayStartForward(item);
  else if ([item action] == @selector(menuPlayStartBackward:))
    validated = renderer->can_do_PlayStartBackward(item);
  else if ([item action] == @selector(menuPlayStop:))
    validated = renderer->can_do_PlayStop(item);
  else if ([item action] == @selector(menuPlayRenderAll:))
    validated = renderer->can_do_PlayRenderAll(item);  
  else if ([item action] == @selector(menuPlayRepeat:))
    validated = renderer->can_do_PlayRepeat(item);
  else if ([item action] == @selector(menuPlayNative:))
    validated = renderer->can_do_PlayNative(item);
  else if ([item action] == @selector(menuPlayCustom:))
    validated = renderer->can_do_PlayCustom(item);
  else if ([item action] == @selector(menuPlayFrameRateUp:))
    validated = renderer->can_do_PlayFrameRateUp(item);
  else if ([item action] == @selector(menuPlayFrameRateDown:))
    validated = renderer->can_do_PlayFrameRateDown(item);
  //-----------------------------------------------------------
  else if ([item action] == @selector(menuStatusToggle:))
    validated = renderer->can_do_StatusToggle(item);
  //-----------------------------------------------------------
  else if ([item action] == @selector(menuWindowDuplicate:))
    validated = renderer->can_do_WindowDuplicate(item);
  else if ([item action] == @selector(menuWindowNext:))
    validated = [self can_do_WindowNext]; 
  //-----------------------------------------------------------
  else
    item_recognized = false;
  
  if (item_recognized && validated)
    renderer->cancel_pending_show();
  return (validated)?YES:NO;
}

/*****************************************************************************/
/*                       kdms_window:becomeKeyWindow                         */
/*****************************************************************************/

- (void) becomeKeyWindow
{
  if (renderer != NULL)
    renderer->set_key_window_status(true);
  [super becomeKeyWindow];
}

/*****************************************************************************/
/*                       kdms_window:resignKeyWindow                         */
/*****************************************************************************/

- (void) resignKeyWindow
{
  if (renderer != NULL)
    renderer->set_key_window_status(false);
  [super resignKeyWindow];
}

/*****************************************************************************/
/*                 kdms_window:scroll_view_BoundsDidChange                   */
/*****************************************************************************/

- (void) scroll_view_BoundsDidChange:(NSNotification *)notification
{
  if (glview)
    [glview reshape];
  if (renderer != NULL)
    renderer->view_dims_changed();
}

@end // kdms_window
