#ifndef FLOW_H
#define FLOW_H

#include "node.h"
#include <unordered_map>

class Packet;
class Ack;
class Probe;
class RetxTimeoutEvent;
class FlowProcessingEvent;

class Flow {

public:
  Flow(uint32_t id, double start_time, uint32_t size,
    Host *s, Host *d);

  ~Flow(); // Destructor

  // TODO implement
  virtual void start_flow();
  virtual void send_pending_data();
  virtual Packet *send(uint32_t seq);
  virtual void send_ack(uint32_t seq, std::vector<uint32_t> sack_list);
  virtual void receive_ack(uint32_t ack, std::vector<uint32_t> sack_list);
  virtual void receive(Packet *p);

  // Only sets the timeout if needed; i.e., flow hasn't finished
  virtual void set_timeout(double time);
  virtual void handle_timeout();
  virtual void cancel_retx_event();

  virtual uint32_t get_priority(uint32_t seq);
  virtual void increase_cwnd();

  uint32_t id;
  double start_time;
  double finish_time;
  uint32_t size;
  Host *src;
  Host *dst;

  uint32_t next_seq_no;
  uint32_t last_unacked_seq;
  RetxTimeoutEvent *retx_event;
  FlowProcessingEvent *flow_proc_event;

//  std::unordered_map<uint32_t, Packet *> packets;

  // Receiver variables
  std::unordered_map<uint32_t, bool> received;
  uint32_t received_bytes;
  uint32_t recv_till;
  uint32_t max_seq_no_recv;
  uint32_t cwnd_mss;
  uint32_t max_cwnd;
  double retx_timeout;
  uint32_t mss;
  uint32_t hdr_size;
  uint32_t total_pkt_sent;
  int size_in_pkt;
  int pkt_drop;
  int data_pkt_drop;

  // Sack
  uint32_t scoreboard_sack_bytes;
  // finished variables
  bool finished;
  double flow_completion_time;

  uint32_t flow_priority;
  bool useDDCTestFlowFinishedEvent;
};






class PFabricFlow : public Flow {
public:
  PFabricFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d);
  uint32_t ssthresh;
  uint32_t count_ack_additive_increase;
  virtual void increase_cwnd();
  virtual void handle_timeout();
};

class PFabricFlowNoSlowStart : public  PFabricFlow {
public:
  PFabricFlowNoSlowStart(uint32_t id, double start_time, uint32_t size, Host *s, Host *d);
  virtual void increase_cwnd();
  virtual void handle_timeout();
};

class FountainFlow : public  Flow {
private:
  double transmission_delay;
  int received_count;
  int min_recv;
  int bytes_acked;
  std::vector<uint32_t> dummySack;


public:
	FountainFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d, double redundancy);
	virtual void start_flow();
	virtual void send_pending_data();
  virtual void receive(Packet *);
  uint32_t ddc_get_priority();
  virtual Packet* send(uint32_t seq);
};

#endif