#include <stdio.h>
#include <time.h>
#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Menu_Window.H>
#include <FL/Fl_Check_Button.H>
#include <FL/gl.h>
//
// Simple GL window with dynamic popup menu
// erco 01/25/06
//

Fl_Button *but;
Fl_Window *mymw;

class MyGlWindow : public Fl_Gl_Window {
    void draw() {
        if (!valid()) {
            glLoadIdentity();
            glViewport(0,0,w(),h());
            glOrtho(-w(),w(),-h(),h(),-1,1);
        }
        glClearColor(0.5, 0.5, 0.5, 0.5);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    int handle(int e) {
        int ret = Fl_Gl_Window::handle(e);
        ret = Fl_Window::handle(e);
        switch ( e ) {
            case FL_PUSH:
              printf ("got button 3 PUSH\n");
              // mymw->position(Fl::event_x_root(), Fl::event_y_root()); 
              mymw->hotspot(but, 1);
              mymw->set_modal();
              mymw->show();
              mymw->show();
              break;
            case FL_RELEASE:
              printf ("got button 3 RELEASE\n");
              break;
        }
        return(ret);
    }
public:
    // CONSTRUCTOR
    MyGlWindow(int X,int Y,int W,int H,const char*L=0) : Fl_Gl_Window(X,Y,W,H,L) {
    }
};

void dismiss (void)
{
  mymw->hide();
}

// MAIN
int main() {
     Fl_Window win(500, 300);
     MyGlWindow mygl(20, 20, win.w()-20, win.h()-20);
     win.end();
     mymw = new Fl_Window(0, 0, 100, 100, "mymw");
     mymw->set_modal();
     mymw->border(0);
     but = new Fl_Check_Button(5, 5, 80, 80, "dismiss");
     but->callback((Fl_Callback*)dismiss);
     mymw->end();
     win.show();
     return(Fl::run());
}
