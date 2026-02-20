//
// testtablerow -- Simple test of Fl_Table_Row class
//

#include "Vp_Demo_Table.h"

// Handle drawing all cells in table
void DemoTable::draw_cell(TableContext context, 
                          int R, int C, int X, int Y, int W, int H)
{
  static char s[50];

  switch ( context )
  {
	case CONTEXT_STARTPAGE:
    fl_font(FL_HELVETICA, 11);
    return;

	case CONTEXT_ROW_HEADER:
    fl_push_clip(X, Y, W, H);
    {
      fl_draw_box(FL_THIN_UP_BOX, X, Y, W, H, color());
      fl_color(FL_BLACK);
      sprintf(s, "row %d ", R);		// text for headers
      fl_draw(s, X, Y, W, H, FL_ALIGN_RIGHT);
    }
    fl_pop_clip();
    return;
	case CONTEXT_COL_HEADER:
    fl_push_clip(X, Y, W, H);
    {
      fl_draw_box(FL_THIN_UP_BOX, X, Y, W, H, color());
      fl_color(FL_BLACK);
      sprintf(s, "col %d", C);		// text for headers
      fl_draw(s, X, Y, W, H, FL_ALIGN_CENTER);
    }
    fl_pop_clip();
    return;

	case CONTEXT_CELL:
	{
    sprintf(s, "%f ", (R+1)/(float)(C+1));		// text for each cell
    fl_push_clip(X, Y, W, H);
    {
      // BG COLOR
      fl_color( row_selected(R) ? selection_color() : FL_WHITE);
      fl_rectf(X, Y, W, H);

      // TEXT
      fl_color(FL_BLACK);
      fl_draw(s, X, Y, W, H, FL_ALIGN_RIGHT);

      // BORDER
      fl_color(FL_LIGHT2); 
      fl_rect(X, Y, W, H);
    }
    fl_pop_clip();
    return;
	}

	default:
    return;
  }
}

void table_cb(Fl_Widget* o, void* data)
{
  Fl_Table *table = (Fl_Table*)data;
  fprintf(stderr, "%s callback: row=%d col=%d, context=%d, event=%d clicks=%d\n",
          (const char*)table->label(),
          (int)table->callback_row(),
          (int)table->callback_col(),
          (int)table->callback_context(),
          (int)Fl::event(),
          (int)Fl::event_clicks());
}

void make_table()
{
  int w = 900, h = 700;
  int t1x = 20, t1y = 20,         t1w = w - 40, t1h = ( (h-60) / 2 );
  int t2x = 20, t2y = t1y+t1h+20, t2w = w - 40, t2h = t1h;

  Fl_Window win(w, h);
  DemoTable table1(t1x, t1y, t1w, t1h, "selected data");
  table1.selection_color(FL_YELLOW);
  table1.when(FL_WHEN_RELEASE);	// handle table events on release
  table1.rows(1001);
  table1.cols(31);
  table1.col_header(1);		// enable col header
  table1.col_resize(4);		// enable col resizing
  table1.row_header(1);		// enable row header
  table1.row_header_width(100);
  table1.row_resize(4);		// enable row resizing
  table1.row_height_all(15);	
  table1.callback(table_cb, (void*)&table1);
  table1.when(FL_WHEN_CHANGED|FL_WHEN_RELEASE);
  table1.end();

  DemoTable table2(t2x, t2y, t2w, t2h, "all data");
  table2.selection_color(FL_YELLOW);
  table2.when(FL_WHEN_RELEASE);	// handle table events on release
  table2.rows(1001);
  table2.cols(31);
  table2.col_header(1);		// enable col header
  table2.col_resize(4);		// enable col resizing
  table2.row_header(1);		// enable row header
  table2.row_header_width(100);
  table2.row_resize(4);		// enable row resizing
  table2.row_height_all(15);	
  table2.callback(table_cb, (void*)&table2);
  table2.when(FL_WHEN_CHANGED|FL_WHEN_RELEASE);
  table2.end();

  win.resizable(table1);
  win.show();
}

