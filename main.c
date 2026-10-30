#include <gtk/gtk.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define MAX_WORKERS 50
typedef struct { pid_t pid; int pipefd; int alive; char status[20]; } Worker;
static Worker workers[MAX_WORKERS];
static int count = 0;
static GtkWidget *tree, *logview, *cpu_label, *mem_label, *entry;
static GtkListStore *store;
enum { PID_COL, PPID_COL, NAME_COL, STATUS_COL, CPU_COL, MEM_COL, N_COLS };

static void log_msg(const char *msg) {
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logview));
    GtkTextIter end; char line[512], stamp[32];
    time_t now = time(NULL); struct tm *t = localtime(&now);
    strftime(stamp, sizeof stamp, "%H:%M:%S", t);
    snprintf(line, sizeof line, "[%s] %s\n", stamp, msg);
    gtk_text_buffer_get_end_iter(b, &end); gtk_text_buffer_insert(b, &end, line, -1);
}
static Worker *get_worker(pid_t pid) {
    for (int i=0;i<count;i++) if (workers[i].pid==pid) return &workers[i];
    return NULL;
}
static void system_stats(void) {
    static unsigned long long old_total=0, old_idle=0;
    FILE *f=fopen("/proc/stat","r");
    unsigned long long a=0,b=0,c=0,d=0,e=0,g=0,h=0,i=0;
    if(f){ fscanf(f,"cpu %llu %llu %llu %llu %llu %llu %llu %llu",&a,&b,&c,&d,&e,&g,&h,&i); fclose(f); }
    unsigned long long total=a+b+c+d+e+g+h+i, idle=d+e;
    double cpu=0;
    if(old_total && total>old_total) cpu=100.0*(total-old_total-(idle-old_idle))/(total-old_total);
    old_total=total; old_idle=idle;
    unsigned long long mt=0, ma=0, val; char key[64], unit[32];
    f=fopen("/proc/meminfo","r");
    if(f){ while(fscanf(f,"%63s %llu %31s",key,&val,unit)==3){ if(!strcmp(key,"MemTotal:"))mt=val; if(!strcmp(key,"MemAvailable:"))ma=val; } fclose(f); }
    char text[160];
    snprintf(text,sizeof text,"CPU Usage: %.1f%%",cpu); gtk_label_set_text(GTK_LABEL(cpu_label),text);
    snprintf(text,sizeof text,"Memory Usage: %.1f%%  (%.0f / %.0f MB)",mt?100.0*(mt-ma)/mt:0,(mt-ma)/1024.0,mt/1024.0);
    gtk_label_set_text(GTK_LABEL(mem_label),text);
}
static void refresh(GtkWidget *unused, gpointer data) {
    (void)unused;(void)data;
    gtk_list_store_clear(store); GtkTreeIter it;
    gtk_list_store_append(store,&it);
    gtk_list_store_set(store,&it,PID_COL,getpid(),PPID_COL,getppid(),NAME_COL,"Process Control GUI",STATUS_COL,"RUNNING",CPU_COL,"-",MEM_COL,"-",-1);
    for(int i=0;i<count;i++){
        Worker *w=&workers[i];
        if(w->alive){ int st; pid_t r=waitpid(w->pid,&st,WNOHANG); if(r==w->pid){w->alive=0;strcpy(w->status,"TERMINATED");if(w->pipefd>=0){close(w->pipefd);w->pipefd=-1;} char m[100];snprintf(m,sizeof m,"Worker %d exited.",w->pid);log_msg(m);} }
        char cpu[32]="-",mem[32]="-";
        if(w->alive){
            char path[80],buf[4096]; snprintf(path,sizeof path,"/proc/%d/stat",w->pid);
            FILE *f=fopen(path,"r");
            if(f && fgets(buf,sizeof buf,f)){
                char *q=strrchr(buf,')'); if(q){ char *p=q+2,*save=NULL,*tok=strtok_r(p," ",&save); int field=3; unsigned long ut=0,st=0; long rss=0;
                    while(tok){if(field==14)ut=strtoul(tok,NULL,10);if(field==15)st=strtoul(tok,NULL,10);if(field==24){rss=strtol(tok,NULL,10);break;}field++;tok=strtok_r(NULL," ",&save);}
                    snprintf(cpu,sizeof cpu,"%.2f s",(double)(ut+st)/sysconf(_SC_CLK_TCK));
                    snprintf(mem,sizeof mem,"%ld KB",rss*(sysconf(_SC_PAGESIZE)/1024));
                }
            } if(f)fclose(f);
        }
        gtk_list_store_append(store,&it);
        gtk_list_store_set(store,&it,PID_COL,w->pid,PPID_COL,getpid(),NAME_COL,"Worker Process",STATUS_COL,w->status,CPU_COL,cpu,MEM_COL,mem,-1);
    }
    system_stats();
}
static pid_t selected_pid(void){
    GtkTreeSelection *s=gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));GtkTreeModel *m;GtkTreeIter it;
    if(!gtk_tree_selection_get_selected(s,&m,&it))return -1;
    gint pid;gtk_tree_model_get(m,&it,PID_COL,&pid,-1);return (pid_t)pid;
}
static void create_worker(GtkButton *b,gpointer d){
    (void)b;(void)d;if(count>=MAX_WORKERS){log_msg("Maximum worker limit reached.");return;}
    int fd[2];if(pipe(fd)<0){log_msg("pipe() failed.");return;}
    pid_t pid=fork();
    if(pid<0){close(fd[0]);close(fd[1]);log_msg("fork() failed.");return;}
    if(pid==0){close(fd[1]);char arg[20];snprintf(arg,sizeof arg,"%d",fd[0]);execl("./worker","worker",arg,(char*)NULL);perror("exec");_exit(1);}
    close(fd[0]);workers[count].pid=pid;workers[count].pipefd=fd[1];workers[count].alive=1;strcpy(workers[count].status,"RUNNING");count++;
    char msg[100];snprintf(msg,sizeof msg,"Created worker process. PID: %d",pid);log_msg(msg);refresh(NULL,NULL);
}
static void send_message(GtkButton *b,gpointer d){
    (void)b;(void)d;pid_t pid=selected_pid();Worker *w=get_worker(pid);
    if(!w||!w->alive||w->pipefd<0){log_msg("Select a running worker first.");return;}
    const char *s=gtk_entry_get_text(GTK_ENTRY(entry));if(!*s){log_msg("Enter a message first.");return;}
    char buf[512];snprintf(buf,sizeof buf,"%s\n",s);
    if(write(w->pipefd,buf,strlen(buf))<0)log_msg("Pipe write failed.");
    else {char m[120];snprintf(m,sizeof m,"Message sent to worker %d.",pid);log_msg(m);}
}
static void control(GtkButton *b,gpointer data){
    (void)b;const char *action=data;pid_t pid=selected_pid();Worker *w=get_worker(pid);
    if(!w||!w->alive){log_msg("Select a running worker first.");return;}
    int sig=!strcmp(action,"Pause")?SIGSTOP:!strcmp(action,"Resume")?SIGCONT:SIGTERM;
    if(kill(pid,sig)==0){if(sig==SIGSTOP)strcpy(w->status,"STOPPED");else if(sig==SIGCONT)strcpy(w->status,"RUNNING");else strcpy(w->status,"TERMINATING");
        char m[120];snprintf(m,sizeof m,"%s signal sent to worker %d.",action,pid);log_msg(m);}
    else {char m[150];snprintf(m,sizeof m,"Action failed: %s",strerror(errno));log_msg(m);}refresh(NULL,NULL);
}
static GtkWidget *button(const char *label,GCallback cb,gpointer data){GtkWidget *w=gtk_button_new_with_label(label);g_signal_connect(w,"clicked",cb,data);return w;}
static void column(const char *title,int id){GtkCellRenderer *r=gtk_cell_renderer_text_new();GtkTreeViewColumn *c=gtk_tree_view_column_new_with_attributes(title,r,"text",id,NULL);gtk_tree_view_column_set_resizable(c,TRUE);gtk_tree_view_append_column(GTK_TREE_VIEW(tree),c);}
static gboolean tick(gpointer d){(void)d;refresh(NULL,NULL);return G_SOURCE_CONTINUE;}
static void activate(GtkApplication *app,gpointer data){
    (void)data;GtkWidget *win=gtk_application_window_new(app);gtk_window_set_title(GTK_WINDOW(win),"Linux Process Communication and Control System");gtk_window_set_default_size(GTK_WINDOW(win),1050,700);gtk_container_set_border_width(GTK_CONTAINER(win),14);
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);gtk_container_add(GTK_CONTAINER(win),box);
    GtkWidget *title=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(title),"<span size='xx-large' weight='bold'>Linux Process Communication and Control System</span>");gtk_box_pack_start(GTK_BOX(box),title,FALSE,FALSE,5);
    GtkWidget *stats=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);cpu_label=gtk_label_new("CPU Usage: --");mem_label=gtk_label_new("Memory Usage: --");gtk_box_pack_start(GTK_BOX(stats),cpu_label,TRUE,TRUE,5);gtk_box_pack_start(GTK_BOX(stats),mem_label,TRUE,TRUE,5);gtk_box_pack_start(GTK_BOX(box),stats,FALSE,FALSE,5);
    store=gtk_list_store_new(N_COLS,G_TYPE_INT,G_TYPE_INT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);
    tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));column("PID",PID_COL);column("PPID",PPID_COL);column("Process",NAME_COL);column("Status",STATUS_COL);column("CPU Time",CPU_COL);column("Memory",MEM_COL);
    GtkWidget *sc=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_vexpand(sc,TRUE);gtk_container_add(GTK_CONTAINER(sc),tree);gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,5);
    GtkWidget *actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,7);
    gtk_box_pack_start(GTK_BOX(actions),button("Create Process",G_CALLBACK(create_worker),NULL),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(actions),button("Pause",G_CALLBACK(control),"Pause"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(actions),button("Resume",G_CALLBACK(control),"Resume"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(actions),button("Terminate",G_CALLBACK(control),"Terminate"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(actions),button("Refresh",G_CALLBACK(tick),NULL),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),actions,FALSE,FALSE,4);
    GtkWidget *ipc=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,7);entry=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(entry),"Enter a message for the selected worker...");gtk_widget_set_hexpand(entry,TRUE);gtk_box_pack_start(GTK_BOX(ipc),entry,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(ipc),button("Send Message",G_CALLBACK(send_message),NULL),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),ipc,FALSE,FALSE,4);
    GtkWidget *lt=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(lt),"<b>System Log</b>");gtk_widget_set_halign(lt,GTK_ALIGN_START);gtk_box_pack_start(GTK_BOX(box),lt,FALSE,FALSE,0);
    logview=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(logview),FALSE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logview),GTK_WRAP_WORD_CHAR);
    GtkWidget *ls=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_size_request(ls,-1,115);gtk_container_add(GTK_CONTAINER(ls),logview);gtk_box_pack_start(GTK_BOX(box),ls,FALSE,TRUE,0);
    log_msg("Application started. Click Create Process to begin.");refresh(NULL,NULL);g_timeout_add_seconds(2,tick,NULL);gtk_widget_show_all(win);
}
int main(int argc,char **argv){
    for(int i=0;i<MAX_WORKERS;i++)workers[i].pipefd=-1;
    GtkApplication *app=gtk_application_new("edu.project.linuxprocesscontrol",G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app,"activate",G_CALLBACK(activate),NULL);int status=g_application_run(G_APPLICATION(app),argc,argv);
    for(int i=0;i<count;i++){if(workers[i].alive){kill(workers[i].pid,SIGTERM);waitpid(workers[i].pid,NULL,0);}if(workers[i].pipefd>=0)close(workers[i].pipefd);}
    g_object_unref(app);return status;
}
