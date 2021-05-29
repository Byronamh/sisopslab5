void initCasioRq(struct casio_rq *casio_rq) {
    casio_rq->casio_rb_root = RB_ROOT;
    INIT_LIST_HEAD(&casio_rq->casio_list_head);
    atomic_set(&casio_rq->nr_running, 0);
}

void AddCasioTaskToList(struct casio_rq *rq, struct task_struct *p) {
    struct list_head *listHeadPtr = NULL;
    struct casio_task *newCasioTask = NULL;
    struct casio_task *casio_task = NULL;
    if (rq && p) {
        newCasioTask = (struct casio_task *) kzalloc(sizeof(struct casio_task), GFP_KERNEL);
        if (newCasioTask) {
            casio_task = NULL;
            newCasioTask->task = p;
            newCasioTask->absolute_deadline = 0;
            list_for_each(listHeadPtr, &rq->casio_list_head)
            {
                casio_task = list_entry(listHeadPtr,
                struct casio_task, casio_list_node);
                if (casio_task) {
                    if (newCasioTask->task->casio_id < casio_task->task->casio_id) {
                        list_add(&newCasioTask->casio_list_node, listHeadPtr);
                        return;
                    }
                }
            }
            list_add(&newCasioTask->casio_list_node, &rq->casio_list_head);
            //logs
        } else {
            printk(KERN_ALERT
            "AddCasioTaskToList: kzalloc\n");
        }
    } else {
        printk(KERN_ALERT
        "AddCasioTaskToList: null pointers\n");
    }
}

void removeCasioTaskFromList(struct casio_rq *rq, struct task_struct *p) {
    struct list_head *listHeadPtr = NULL;
    struct list_head *next = NULL;
    struct casio_task *casio_task = NULL;
    if (rq && p) {
        list_for_each_safe(listHeadPtr, next, &rq->casio_list_head)
        {
            casio_task = list_entry(listHeadPtr,
            struct casio_task, casio_list_node);
            if (casio_task) {
                if (casio_task->task->casio_id == p->casio_id) {
                    list_del(listHeadPtr);
                    //logs
                    kfree(casio_task);
                    return;
                }
            }
        }
    }
}

struct casio_task *findCasioTaskList(struct casio_rq *rq, struct task_struct *p) {
    struct list_head *listHeadPtr = NULL;
    struct casio_task *casio_task = NULL;
    if (rq && p) {
        list_for_each(listHeadPtr, &rq->casio_list_head)
        {
            casio_task = list_entry(listHeadPtr,
            struct casio_task, casio_list_node);
            if (casio_task) {
                if (casio_task->task->casio_id == p->casio_id) {
                    return casio_task;
                }
            }
        }
    }
    return NULL;
}

void putCasioIntoRBTree(struct casio_rq *rq, struct casio_task *p) {
    struct rb_node **node = NULL;
    struct rb_node *parent = NULL;
    struct casio_task *entry = NULL;
    node = &rq->casio_rb_root.rb_node;
    while (*node != NULL) {
        parent = *node;
        entry = rb_entry(parent,
        struct casio_task, casio_rb_node);
        if (entry) {
            if (p->absolute_deadline < entry->absolute_deadline) {
                node = &parent->rb_left;
            } else {
                node = &parent->rb_right;
            }
        }
    }
    rb_link_node(&p->casio_rb_node, parent, node);
    rb_insert_color(&p->casio_rb_node, &rq->casio_rb_root);
}

void removeCasioTaskFromRBTree(struct casio_rq *rq, struct casio_task *p) {
    rb_erase(&(p->casio_rb_node), &(rq->casio_rb_root));
    p->casio_rb_node.rb_left = p->casio_rb_node.rb_right = NULL;
}

struct casio_task *earliestDearlineCasioRBTask(struct casio_rq *rq) {
    struct rb_node *node = NULL;
    struct casio_task *p = NULL;
    node = rq->casio_rb_root.rb_node;
    if (node == NULL)
        return NULL;
    while (node->rb_left != NULL) {
        node = node->rb_left;
    }
    p = rb_entry(node,
    struct casio_task, casio_rb_node);
    return p;
}

static void addCasiTaskToList(struct rq *rq, struct task_struct *p, int wakeup) {
    struct casio_task *t = NULL;
    if (p) {
        t = findCasioTaskList(&rq->casio_rq, p);
        if (t) {
            t->absolute_deadline = sched_clock() + p->deadline;
            putCasioIntoRBTree(&rq->casio_rq, t);
            atomic_inc(&rq->casio_rq.nr_running);
            //logs
        } else {
            printk(KERN_ALERT "addCasiTaskToList\n");
        }
    }
}

static void dequeueCasioTask(struct rq *rq, struct task_struct *p, int sleep) {
    struct casio_task *t = NULL;
    if (p) {
        t = findCasioTaskList(&rq->casio_rq, p);
        if (t) {
            removeCasioTaskFromRBTree(&rq->casio_rq, t);
            atomic_dec(&rq->casio_rq.nr_running);
            if (t->task->state == TASK_DEAD || t->task->state == EXIT_DEAD
                || t->task->state == EXIT_ZOMBIE) {
                removeCasioTaskFromList(&rq->casio_rq, t->task);
            }
        } else {
            printk(KERN_ALERT "dequeueCasioTask\n");
        }
    }
}

static void checkPreemptCurrentCasio(struct rq *rq, struct task_struct *p) {
    struct casio_task *t = NULL;
    struct casio_task *curr = NULL;
    if (rq->curr->policy != SCHED_CASIO) {
        resched_task(rq->curr);
    } else {
        t = earliestDearlineCasioRBTask(&rq->casio_rq);
        if (t) {
            curr = findCasioTaskList(&rq->casio_rq, rq->curr);
            if (curr) {
                if (t->absolute_deadline < curr->absolute_deadline)
                    resched_task(rq->curr);
            } else {
                printk(KERN_ALERT
                "checkPreemptCurrentCasio\n");
            }
        }
    }
}

static struct task_struct *pickNextCasioTask(struct rq *rq) {
    struct casio_task *t = NULL;
    t = earliestDearlineCasioRBTask(&rq->casio_rq);
    if (t) {
        return t->task;
    }
    return NULL;
}

#ifdef CONFIG_SMP
static unsigned long loadBalanceCasio(
        struct rq* this_rq, int this_cpu,
                struct rq* busiest,
                unsigned long max_load_move,
                struct sched_domain* sd, enum cpu_idle_type idle,
                int* all_pinned, int* this_best_prio){
        return 0;
}
static int moveOneTaskCasio(
        struct rq* this_rq, int this_cpu,
                struct rq* busiest,
                struct sched_domain* sd,
                enum cpu_idle_type idle)
{
        return 0;
}

#endif
const struct sched_class casio_sched_class = {
        .next                   = &rt_sched_class,
        .enqueue_task           = addCasiTaskToList,
        .dequeue_task           = dequeueCasioTask,
        .check_preempt_curr     = checkPreemptCurrentCasio,
        .pick_next_task         = pickNextCasioTask,
#ifdef CONFIG_SMP
        .load_balance           = loadBalanceCasio,
        .move_one_task          = moveOneTaskCasio,
#endif
};
struct casio_event_log casio_event_log;

struct casio_event_log *getCasioEventLog() {
    return &casio_event_log;
}

void registerCasioEvent(unsigned long long t, char *m, int a) {
    if (casio_event_log.lines < CASIO_MAX_EVENT_LINES) {
        casio_event_log.casio_event[casio_event_log.lines].action = a;
        casio_event_log.casio_event[casio_event_log.lines].timestamp = t;
        strncpy(casio_event_log.casio_event[casio_event_log.lines].msg, m, CASIO_MSG_SIZE – 1);
        casio_event_log.lines++;
    } else {
        printk(KERN_ALERT
        "registerCasioEvent: full\n");
    }
}

void initCasioEventLog() {
    char msg[CASIO_MSG_SIZE];
    casio_event_log.lines = casio_event_log.cursor = 0;
    snprintf(msg, CASIO_MSG_SIZE, "initCasioEventLog: (%lu:%lu)", casio_event_log.lines, casio_event_log.cursor);
    registerCasioEvent(sched_clock(), msg, CASIO_MSG);
}

snprintf(msg, CASIO_MSG_SIZE, "mensaje %d %lu", valor1, valor2);

registerCasioEvent(sched_clock(), msg, CASIO_MSG);
char msg[CASIO_MSG_SIZE];