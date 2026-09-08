#include <unistd.h>
#include <string.h>

struct expectation { int result, index, option, argument, offset; };
struct example {
    const char *name, *options;
    int noisy, count;
    char *args[6];
    unsigned steps;
    struct expectation expected[5];
};
#define E(r,i,o,a,p) {r,i,o,a,p}
static const struct example examples[] = {
    {"attached", "qn:", 1, 2, {"argprobe","-n10"}, 2,
        {E('n',2,'n',1,2),E(-1,2,'n',0,0)}},
    {"separate", "qn:", 1, 3, {"argprobe","-n","10"}, 2,
        {E('n',3,'n',2,0),E(-1,3,'n',0,0)}},
    {"cluster", "qn:", 1, 2, {"argprobe","-qn10"}, 3,
        {E('q',1,'q',0,0),E('n',2,'n',1,3),E(-1,2,'n',0,0)}},
    {"cluster-separate", "qn:", 1, 3, {"argprobe","-qn","10"}, 3,
        {E('q',1,'q',0,0),E('n',3,'n',2,0),E(-1,3,'n',0,0)}},
    {"dash-value", "qn:", 1, 3, {"argprobe","-n","-q"}, 2,
        {E('n',3,'n',2,0),E(-1,3,'n',0,0)}},
    {"double-value", "qn:", 1, 4, {"argprobe","-n","--","-q"}, 3,
        {E('n',3,'n',2,0),E('q',4,'q',0,0),E(-1,4,'q',0,0)}},
    {"empty-value", "qn:", 1, 3, {"argprobe","-n",""}, 2,
        {E('n',3,'n',2,0),E(-1,3,'n',0,0)}},
    {"missing", "qn:", 1, 2, {"argprobe","-n"}, 2,
        {E('?',2,'n',0,0),E(-1,2,'n',0,0)}},
    {"missing-cluster", "qn:", 1, 2, {"argprobe","-qn"}, 3,
        {E('q',1,'q',0,0),E('?',2,'n',0,0),E(-1,2,'n',0,0)}},
    {"missing-after", "qn:", 1, 3, {"argprobe","-n10","-n"}, 3,
        {E('n',2,'n',1,2),E('?',3,'n',0,0),E(-1,3,'n',0,0)}},
    {"attached-dash", "qn:", 1, 2, {"argprobe","-n-q"}, 2,
        {E('n',2,'n',1,2),E(-1,2,'n',0,0)}},
    {"missing-colon", ":qn:", 1, 2, {"argprobe","-n"}, 2,
        {E(':',2,'n',0,0),E(-1,2,'n',0,0)}},
    {"missing-quiet", "qn:", 0, 2, {"argprobe","-n"}, 2,
        {E('?',2,'n',0,0),E(-1,2,'n',0,0)}},
    {"unknown", "qn:", 1, 4, {"argprobe","-n10","-z","-q"}, 4,
        {E('n',2,'n',1,2),E('?',3,'z',0,0),E('q',4,'q',0,0),E(-1,4,'q',0,0)}},
    {"unknown-colon", ":qn:", 1, 2, {"argprobe","-z"}, 2,
        {E('?',2,'z',0,0),E(-1,2,'z',0,0)}},
    {"unknown-quiet", "qn:", 0, 2, {"argprobe","-z"}, 2,
        {E('?',2,'z',0,0),E(-1,2,'z',0,0)}},
    {"colon-is-syntax", ":qn:", 1, 2, {"argprobe","-:"}, 2,
        {E('?',2,':',0,0),E(-1,2,':',0,0)}},
    {"operand", "qn:", 1, 3, {"argprobe","file","-n10"}, 2,
        {E(-1,1,0,0,0),E(-1,1,0,0,0)}},
    {"dash", "qn:", 1, 3, {"argprobe","-","-n10"}, 1,
        {E(-1,1,0,0,0)}},
    {"double", "qn:", 1, 3, {"argprobe","--","-n10"}, 1,
        {E(-1,2,0,0,0)}},
    {"attached-rest", "qn:", 1, 2, {"argprobe","-n10q"}, 2,
        {E('n',2,'n',1,2),E(-1,2,'n',0,0)}},
    {"flag-after", "qn:", 1, 3, {"argprobe","-n10","-q"}, 3,
        {E('n',2,'n',1,2),E('q',3,'q',0,0),E(-1,3,'q',0,0)}},
    {"unknown-cluster", "qn:", 1, 2, {"argprobe","-zqn10"}, 4,
        {E('?',1,'z',0,0),E('q',1,'q',0,0),E('n',2,'n',1,4),E(-1,2,'n',0,0)}},
    {"no-permutation", "qn:", 1, 4, {"argprobe","-n10","file","-q"}, 3,
        {E('n',2,'n',1,2),E(-1,2,'n',0,0),E(-1,2,'n',0,0)}},
    {"empty", "qn:", 1, 1, {"argprobe"}, 1,
        {E(-1,1,0,0,0)}}
};
#undef E

/* Static strings keep a real mid-cluster cursor and borrowed optarg alive
   across separate ordinary calls around scheduler yields in the module. */
static char *peer_args[] = {"argpeer", "-c", "-different", NULL};
static char *owner_args[] = {"argowner", "-qn10", "-q", NULL};
static char *exec_args[] = {"argexec", "-n10", "-qv", NULL};
static char *after_args[] = {"argafter", "-n", "20", NULL};

int main(int argc, char **argv)
{
    size_t i, step;
    if (argc != 2) return 90;
    if (strcmp(argv[1], "owner-start") == 0)
        return getopt(3, owner_args, "qn:") == 'q' && optind == 1 &&
               optarg == NULL && optopt == 'q' ? 0 : 71;
    if (strcmp(argv[1], "owner-argument") == 0)
        return getopt(3, owner_args, "qn:") == 'n' && optind == 2 &&
               optarg == owner_args[1] + 3 && strcmp(optarg, "10") == 0 ? 0 : 72;
    if (strcmp(argv[1], "owner-retained") == 0)
        return optind == 2 && optopt == 'n' &&
               optarg == owner_args[1] + 3 && strcmp(optarg, "10") == 0 ? 0 : 73;
    if (strcmp(argv[1], "owner-finish") == 0)
        return getopt(3, owner_args, "qn:") == 'q' && optind == 3 &&
               optarg == NULL && getopt(3, owner_args, "qn:") == -1 ? 0 : 74;
    if (strcmp(argv[1], "peer") == 0)
        return getopt(3, peer_args, "c:") == 'c' && optind == 3 &&
               optopt == 'c' && optarg == peer_args[2] ? 0 : 75;
    if (strcmp(argv[1], "exec-seed") == 0 || strcmp(argv[1], "exec-cluster") == 0) {
        if (getopt(3, exec_args, "n:qv") != 'n' || optarg != exec_args[1] + 2)
            return 76;
        if (strcmp(argv[1], "exec-cluster") == 0 &&
            (getopt(3, exec_args, "n:qv") != 'q' || optind != 2 || optarg != NULL))
            return 77;
        opterr = 0;
        return 0;
    }
    if (strcmp(argv[1], "exec-after") == 0) {
        if (optind != 1 || opterr != 1 || optopt != 0 || optarg != NULL)
            return 78;
        return getopt(3, after_args, "n:") == 'n' && optind == 3 &&
               optarg == after_args[2] && strcmp(optarg, "20") == 0 ? 0 : 79;
    }
    for (i = 0; i < sizeof(examples) / sizeof(examples[0]); ++i) {
        const struct example *example = &examples[i];
        if (strcmp(argv[1], example->name) != 0) continue;
        opterr = example->noisy;
        optarg = (char *)"stale";
        for (step = 0; step < example->steps; ++step) {
            const struct expectation *expected = &example->expected[step];
            int result = getopt(example->count, example->args, example->options);
            char *argument = expected->argument == 0 ? NULL :
                example->args[expected->argument] + expected->offset;
            if (result != expected->result || optind != expected->index ||
                optopt != expected->option || optarg != argument) return 11 + (int)step;
        }
        return 0;
    }
    return 91;
}
