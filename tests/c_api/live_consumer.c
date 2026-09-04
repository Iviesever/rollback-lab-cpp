#include <rollback_lab/c_api/rollback_lab_c.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INIT(value) do { memset(&(value),0,sizeof(value)); (value).api_version=RL_API_VERSION; (value).struct_size=sizeof(value); } while (0)
#define REQUIRE(call) do { rl_status status=(call); if(status!=RL_OK) { fprintf(stderr,"%s failed: %u\n",#call,status); result=1; goto cleanup; } } while (0)

static int parse_u64(const char* text, uint64_t* output) {
    char* end = NULL;
    unsigned long long parsed;
    if (!text || text[0]<'0' || text[0]>'9') return 0;
    errno=0; parsed=strtoull(text,&end,10);
    if (errno || *end!='\0') return 0;
    *output=(uint64_t)parsed; return 1;
}

static int write_artifact(rl_live* live, const char* directory, int kind) {
    const char* name = kind==0 ? "report.json" : (kind==1 ? "trace.json" : "input.rlr");
    uint32_t required=0;
    rl_status status;
    void* bytes;
    FILE* file=NULL;
    char path[4096];
    int length=snprintf(path,sizeof(path),"%s/%s",directory,name);
    if(length<0 || (size_t)length>=sizeof(path)) return 0;
    status=kind==0 ? rl_live_copy_report(live,NULL,0,&required) :
           kind==1 ? rl_live_copy_trace(live,NULL,0,&required) : rl_live_copy_replay(live,NULL,0,&required);
    if(status!=RL_BUFFER_TOO_SMALL || required==0 || required>(128U<<20U)) return 0;
    bytes=malloc(required);
    if(!bytes) return 0;
    status=kind==0 ? rl_live_copy_report(live,(char*)bytes,required,&required) :
           kind==1 ? rl_live_copy_trace(live,(char*)bytes,required,&required) :
                     rl_live_copy_replay(live,(uint8_t*)bytes,required,&required);
    if(status!=RL_OK) { free(bytes); return 0; }
#ifdef _WIN32
    if(fopen_s(&file,path,"wb")!=0) file=NULL;
#else
    file=fopen(path,"wb");
#endif
    if(!file) { free(bytes); return 0; }
    if(kind!=2) --required;
    {
        const int written=fwrite(bytes,1,required,file)==required;
        const int closed=fclose(file)==0;
        free(bytes);
        return written && closed;
    }
}

int main(int argc, char** argv) {
    int result=0;
    rl_session* a=NULL;
    rl_session* b=NULL;
    rl_live* live=NULL;
    rl_session_config session;
    rl_live_config config;
    rl_live_step_result step;
    rl_hash_result ha,hb;
    rl_metrics ma,mb;
    uint64_t scenario,transport,frames;
    uint32_t tick;
    if(argc!=5 || !parse_u64(argv[1],&scenario) || !parse_u64(argv[2],&transport) ||
       !parse_u64(argv[3],&frames) || frames==0 || frames>36000U) {
        fprintf(stderr,"usage: rollback_lab_c_demo scenario_seed transport_seed frames existing_output_directory\n"); return 2;
    }
    INIT(session); session.max_rollback_frames=120;
    REQUIRE(rl_session_create(&session,&a));
    session.local_peer=RL_PEER_B; REQUIRE(rl_session_create(&session,&b));
    INIT(config); config.scenario_seed=scenario; config.transport_seed=transport;
    config.frame_count=(uint32_t)frames; config.base_latency_ticks=5; config.jitter_ticks=3;
    config.loss_percent=5; config.reorder_percent=10; config.duplicate_percent=5; config.burst_loss_percent=1;
    config.max_queue_packets=4096; config.max_queue_bytes=4U<<20U;
    config.bandwidth_bytes_per_tick=1U<<20U; config.max_packet_age_ticks=600; config.tail_redundancy_ticks=64;
    REQUIRE(rl_live_create(&config,a,b,&live));
    INIT(step);
    for(tick=0;tick<config.frame_count+96U;++tick) REQUIRE(rl_live_step(live,0,0,&step));
    INIT(ha); INIT(hb); INIT(ma); INIT(mb);
    REQUIRE(rl_session_get_hash(a,&ha)); REQUIRE(rl_session_get_hash(b,&hb));
    REQUIRE(rl_session_get_metrics(a,&ma)); REQUIRE(rl_session_get_metrics(b,&mb));
    if(!step.finished || step.desync_detected || ha.state_hash!=hb.state_hash ||
       ma.confirmed_frame!=config.frame_count || mb.confirmed_frame!=config.frame_count) { result=3; goto cleanup; }
    if(!write_artifact(live,argv[4],0) || !write_artifact(live,argv[4],1) || !write_artifact(live,argv[4],2)) { result=4; goto cleanup; }
    printf("C ABI confirmed=%u hash=0x%016llX rollbacks=%u\n",ma.confirmed_frame,
           (unsigned long long)ha.state_hash,ma.rollback_count+mb.rollback_count);
cleanup:
    if(live && rl_live_destroy(live)!=RL_OK) result=5;
    if(a && rl_session_destroy(a)!=RL_OK) result=5;
    if(b && rl_session_destroy(b)!=RL_OK) result=5;
    return result;
}
