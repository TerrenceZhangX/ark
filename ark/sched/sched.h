#ifndef ARK_SCHED_H_
#define ARK_SCHED_H_

#include "ark/gpu/gpu_kernel.h"
#include "ark/gpu/gpu_mgr.h"
#include "ark/model.h"
#include "ark/sched/sched_codegen.h"
#include "ark/sched/sched_opgraph.h"
#include "ark/sched/sched_profiler.h"

namespace ark {

struct BufInfo
{
    // all the information of a GPU data buffer
    BufInfo(int gpu_id_, size_t bytes_, TensorBuf *tbuf_, int sid_,
            size_t offset_)
        : gpu_id{gpu_id_}, bytes{bytes_}, tbuf{tbuf_}, sid{sid_}, offset{
                                                                      offset_}
    {
    }
    // gpu_id: the id of the GPU where the buffer is allocated. If the
    // gpu_id is the same as this rank's gpu_id, the buffer is allocated on
    // the GPU, otherwise it will be imported from another GPU.
    int gpu_id;
    size_t bytes;
    TensorBuf *tbuf;
    // sid: a unique id of the buffer, used to identify the buffer when
    // we need to export or import the buffer. If the TensorBuf is located
    // on this GPU and the sid is not -1, it will be exported.
    int sid;
    size_t offset;
};

class SchedulerBase
{
  public:
    SchedulerBase(const int gpu_id, int rank_, int world_size_,
                  unsigned int wps_ = 16)
        : gpu_mgr{get_gpu_mgr(gpu_id)}, rank{rank_}, world_size{world_size_}
    {
    }
    // create context on gpu for the model
    virtual GpuMgrCtx *create_context(const std::string &name) = 0;
    //
    virtual std::vector<std::string> schedule() = 0;

    virtual GpuBuf *get_gpu_buf(Tensor *tns) const = 0;
    // the num of the depth is equal to the num of the op
    virtual unsigned int get_num_depths() const = 0;

    // This function is used to configure the TensorBuf. The TensorBuf is an
    // abstraction of the GPU memory, it correspond to a memory region on the
    // _ARK_BUF. This function will configure the allocation, import and export
    // of the TensorBuf and stores the TensorBuf information in buf_infos for
    // later use
    virtual void configure_gpu_buf() = 0;
    virtual Tensor *get_tensor(Tensor *tns) const = 0;

  protected:
    GpuMgr *gpu_mgr;

    // Model model;
    // the information of the GPU buffers
    std::vector<BufInfo> buf_infos;
    // map from TensorBuf to gpu buffer, the TensorBuf is an abstract of the
    // data buffer in the model layer, and the GpuBuf is the real buffer in the
    // GPU address space
    std::map<TensorBuf *, GpuBuf *> buf_trans;

    int rank;
    int world_size;
    unsigned int wps;
};

class SimpleScheduler : public SchedulerBase
{
  public:
    SimpleScheduler(const int gpu_id, int rank_, int world_size_,
                    const Model &model, unsigned int wps_ = 16);
    void create_sched_opseq(const Model &model, const GpuInfo &gpu_info);

    // create context on gpu for the model
    GpuMgrCtx *create_context(const std::string &name);
    //
    std::vector<std::string> schedule();

    Tensor *get_tensor(Tensor *tns) const
    {
        return tns;
    }
    GpuBuf *get_gpu_buf(Tensor *tns) const;
    // the num of the depth is equal to the num of the op
    unsigned int get_num_depths() const
    {
        return sched_opseqs.size();
    }
    std::vector<SchedOp> &get_sched_ops()
    {
        return sched_ops;
    }
    unsigned int get_num_sched_ops()
    {
        return sched_ops.size();
    }
    std::vector<SchedOpSeq> &get_sched_opseqs()
    {
        return sched_opseqs;
    }
    std::vector<BufInfo> &get_buf_infos()
    {
        return buf_infos;
    }
    void schedule_sched_opseq(SchedOpSeq &sop, int max_wps, int max_sm_num,
                              std::vector<Sched> &scheds);
    // This function is used to configure the TensorBuf. The TensorBuf is an
    // abstraction of the GPU memory, it correspond to a memory region on the
    // _ARK_BUF. This function will configure the allocation, import and export
    // of the TensorBuf and stores the TensorBuf information in buf_infos for
    // later use
    void configure_gpu_buf();

  private:
    std::vector<SchedOpSeq> sched_opseqs;
    std::vector<SchedOp> sched_ops;

    SimpleCodeGenerator codegen;
    // DefaultCodeGenerator scg;
};

class DefaultScheduler : public SchedulerBase
{
  public:
    DefaultScheduler(const int gpu_id, int rank_, int world_size_,
                     const Model &model, unsigned int wps = 16);

    std::vector<std::string> schedule();
    GpuMgrCtx *create_context(const std::string &name);
    Tensor *get_tensor(Tensor *tns) const;
    GpuBuf *get_gpu_buf(Tensor *tns) const;
    unsigned int get_num_depths() const;

  protected:
    Model *optimize_model(const Model &model);
    void configure_gpu_buf();
    void schedule_depth(std::vector<SchedOpSeq *> &depth,
                        std::vector<Sched> &scheds);
    void schedule_depth_comm(std::vector<SchedOpSeq *> &depth,
                             std::vector<Sched> &scheds);

    Model *opt_model;
    size_t dbytes;
    OpGraph *op_graph;

    std::map<Tensor *, Tensor *> tns_trans;
    std::vector<const Op *> send_recv_ops;
    GpuMgrCtx *ctx;
    const std::string model_path = "model.json";
    DefaultCodeGenerator scg;
    unsigned int wps;
};

class KahyparScheduler : public DefaultScheduler
{
  public:
    KahyparScheduler(const int gpu_id, int rank_, int world_size_,
                     const Model &model, unsigned int wps = 16);
    std::vector<std::string> schedule();

  private:
    std::vector<Sched> simplify_sched(std::vector<Sched> &original_scheds);

    int kahypar_schedule_depth(std::vector<SchedOpSeq *> &depth,
                               std::vector<Sched> &scheds);
    SchedProfiler profiler;
};

} // namespace ark

#endif // ARK_SCHED_H_
