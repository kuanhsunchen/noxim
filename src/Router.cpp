/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2010 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the router
 */

#include "Router.h"
#include "routingAlgorithms/RoutingAlgorithms.h"


void Router::rxProcess()
{
    if (reset.read()) {
	// Clear outputs and indexes of receiving protocol
	for (int i = 0; i < DIRECTIONS + 2; i++) {
	    ack_rx[i].write(0);
	    current_level_rx[i] = 0;
	}
	routed_flits = 0;
	local_drained = 0;
    } else {
	// For each channel decide if a new flit can be accepted
	//
	// This process simply sees a flow of incoming flits. All arbitration
	// and wormhole related issues are addressed in the txProcess()

	for (int i = 0; i < DIRECTIONS + 2; i++) {
	    // To accept a new flit, the following conditions must match:
	    //
	    // 1) there is an incoming request
	    // 2) there is a free slot in the input buffer of direction i

	    if ((req_rx[i].read() == 1 - current_level_rx[i])
		&& !buffer[i].IsFull()) {
		Flit received_flit = flit_rx[i].read();

		if (GlobalParams::verbose_mode > VERBOSE_OFF) {
			LOG << "Input[" << i << "], Received flit: " << received_flit << endl;
		}
		// Store the incoming flit in the circular buffer
		buffer[i].Push(received_flit);

		// Negate the old value for Alternating Bit Protocol (ABP)
		current_level_rx[i] = 1 - current_level_rx[i];

		// Incoming flit
		stats.power.Buffering();

		if (received_flit.src_id == local_id)
		  stats.power.EndToEnd();
	    }
	    ack_rx[i].write(current_level_rx[i]);
	}
    }
    stats.power.Leakage();
}



void Router::txProcess()
{
  if (reset.read()) 
    {
      // Clear outputs and indexes of transmitting protocol
      for (int i = 0; i < DIRECTIONS + 2; i++) 
	{
	  req_tx[i].write(0);
	  current_level_tx[i] = 0;
	}
    } 
  else 
    {
      // 1st phase: Reservation
      for (int j = 0; j < DIRECTIONS + 2; j++) 
	{
	  int i = (start_from_port + j) % (DIRECTIONS + 2);
	 
	  if (!buffer[i].checkDeadlock())
	  {
	      LOG << " deadlock on buffer " << i << endl;
	  }

	  if (!buffer[i].IsEmpty()) 
	    {

	      Flit flit = buffer[i].Front();

	      if (flit.flit_type == FLIT_TYPE_HEAD) 
		{
		  // prepare data for routing
		  RouteData route_data;
		  route_data.current_id = local_id;
		  route_data.src_id = flit.src_id;
		  route_data.dst_id = flit.dst_id;
		  route_data.dir_in = i;

		  int o = route(route_data);

		  if ( o==DIRECTION_HUB)
		  {
		      LOG << "Ready to reserve HUB direction ..." << endl;

		  }

		  if (reservation_table.isAvailable(o)) 
		  {
		      stats.power.Crossbar();
		      reservation_table.reserve(i, o);
		      if (GlobalParams::verbose_mode > VERBOSE_OFF) 
		      {
			      LOG << "Input[" << i << "] (" << buffer[i].
			      Size() << " flits)" << ", reserved Output["
			      << o << "], flit: " << flit << endl;
		      }
		  }
		}
	    }
	}
      start_from_port++;

      // 2nd phase: Forwarding
      for (int i = 0; i < DIRECTIONS + 2; i++) 
	{
	  if (!buffer[i].IsEmpty()) 
	    {
	      Flit flit = buffer[i].Front();

	      int o = reservation_table.getOutputPort(i);
	      if (o != NOT_RESERVED) 
	      {
		  if (current_level_tx[o] == ack_tx[o].read()) 
		  {
		    if (o == DIRECTION_HUB)
		    {
			  LOG << "Forwarding to HUB " << endl;
		  /* TODO: adapt code to new model
			// Forward flit to WiNoC
			if (winoc->CanTransmit(local_id))
			{
			    if (GlobalParams::verbose_mode > VERBOSE_OFF) 
			    {
				    LOG << "Input[" << i <<
				    "] forward to Output[" << o << "], flit: "
				    << flit << endl;
			    }
			    // LOG << "Inject to RH: Router ID " << local_id << ", Type " << flit.flit_type << ", " << flit.src_id << "-->" << flit.dst_id << endl;

			    winoc->InjectToRadioHub(local_id, flit);
			    buffer[i].Pop();

			    if (flit.flit_type == FLIT_TYPE_TAIL)
				reservation_table.release(o);
			}
		    */
		    }
		    if (GlobalParams::verbose_mode > VERBOSE_OFF) 
		    {
			    LOG << "Input[" << i <<
			    "] forward to Output[" << o << "], flit: "
			    << flit << endl;
		    }

		    flit_tx[o].write(flit);
		    current_level_tx[o] = 1 - current_level_tx[o];
		    req_tx[o].write(current_level_tx[o]);
		    buffer[i].Pop();

		    stats.power.Link();

		    if (flit.dst_id == local_id)
			stats.power.EndToEnd();

		    if (flit.flit_type == FLIT_TYPE_TAIL)
			reservation_table.release(o);

		    // Update stats
		    if (o == DIRECTION_LOCAL) 
		    {
			LOG << "Consumed flit src " << flit.src_id << " dst = " << flit.dst_id << endl;
			stats.receivedFlit(sc_time_stamp().
				to_double() / 1000, flit);
			if (GlobalParams::
				max_volume_to_be_drained) 
			{
			    if (drained_volume >=
				    GlobalParams::
				    max_volume_to_be_drained)
				sc_stop();
			    else 
			    {
				drained_volume++;
				local_drained++;
			    }
			}
		    } 
		    else if (i != DIRECTION_LOCAL) 
		    {
			// Increment routed flits counter
			routed_flits++;
		    }
		  }
	      }
	    }
	}
      /* TODO: move this code as a normal direction
      // 3rd phase: Consume incoming flits from WiNoC
      if (GlobalParams::use_winoc &&
      winoc->FlitAvailable(local_id))
      {
      Flit flit = winoc->GetFlit(local_id);
      stats.receivedFlit(sc_time_stamp().
      to_double() / 1000, flit);
      }
       */
    }				// else reset read
  stats.power.Leakage();
}

NoP_data Router::getCurrentNoPData()
{
    NoP_data NoP_data;

    for (int j = 0; j < DIRECTIONS; j++) {
	NoP_data.channel_status_neighbor[j].free_slots =
	    free_slots_neighbor[j].read();
	NoP_data.channel_status_neighbor[j].available =
	    (reservation_table.isAvailable(j));
    }

    NoP_data.sender_id = local_id;

    return NoP_data;
}

void Router::bufferMonitor()
{
    if (reset.read()) {
	for (int i = 0; i < DIRECTIONS + 1; i++)
	    free_slots[i].write(buffer[i].GetMaxBufferSize());
    } else {

	if (GlobalParams::selection_strategy == SEL_BUFFER_LEVEL ||
	    GlobalParams::selection_strategy == SEL_NOP) {

	    // update current input buffers level to neighbors
	    for (int i = 0; i < DIRECTIONS + 1; i++)
		free_slots[i].write(buffer[i].getCurrentFreeSlots());

	    // NoP selection: send neighbor info to each direction 'i'
	    NoP_data current_NoP_data = getCurrentNoPData();

	    for (int i = 0; i < DIRECTIONS; i++)
		NoP_data_out[i].write(current_NoP_data);
	}
    }
}

vector < int > Router::routingFunction(const RouteData & route_data)
{
    // TODO: check 
    // If WiNoC available, check for intercluster communication
    if (GlobalParams::use_winoc)
    {
        if (hasRadioHub(local_id) &&
                hasRadioHub(route_data.dst_id) &&
                !sameRadioHub(local_id,route_data.dst_id)
           )
        {
            LOG << "Setting direction hub to reach destination node " << route_data.dst_id << endl;

            vector<int> dirv;
            dirv.push_back(DIRECTION_HUB);
            return dirv;
        }
    }
    LOG << "Wired routing for dst = " << route_data.dst_id << endl;

    RoutingAlgorithm * routingAlgorithm = RoutingAlgorithms::get(GlobalParams::routing_algorithm);

    if (routingAlgorithm == 0)
        assert(false);

    return routingAlgorithm->route(this, route_data);
}

int Router::route(const RouteData & route_data)
{
    stats.power.Routing();

    if (route_data.dst_id == local_id)
	return DIRECTION_LOCAL;

    vector < int >candidate_channels = routingFunction(route_data);

    return selectionFunction(candidate_channels, route_data);
}

void Router::NoP_report() const
{
    NoP_data NoP_tmp;
	LOG << "NoP report: " << endl;

    for (int i = 0; i < DIRECTIONS; i++) {
	NoP_tmp = NoP_data_in[i].read();
	if (NoP_tmp.sender_id != NOT_VALID)
	    cout << NoP_tmp;
    }
}

//---------------------------------------------------------------------------

int Router::NoPScore(const NoP_data & nop_data,
			  const vector < int >&nop_channels) const
{
    int score = 0;

    for (unsigned int i = 0; i < nop_channels.size(); i++) {
	int available;

	if (nop_data.channel_status_neighbor[nop_channels[i]].available)
	    available = 1;
	else
	    available = 0;

	int free_slots =
	    nop_data.channel_status_neighbor[nop_channels[i]].free_slots;

	score += available * free_slots;
    }

    return score;
}

int Router::selectionNoP(const vector < int >&directions,
			      const RouteData & route_data)
{
    vector < int >neighbors_on_path;
    vector < int >score;
    int direction_selected = NOT_VALID;

    int current_id = route_data.current_id;

    for (size_t i = 0; i < directions.size(); i++) {
	// get id of adjacent candidate
	int candidate_id = getNeighborId(current_id, directions[i]);

	// apply routing function to the adjacent candidate node
	RouteData tmp_route_data;
	tmp_route_data.current_id = candidate_id;
	tmp_route_data.src_id = route_data.src_id;
	tmp_route_data.dst_id = route_data.dst_id;
	tmp_route_data.dir_in = reflexDirection(directions[i]);


	vector < int >next_candidate_channels =
	    routingFunction(tmp_route_data);

	// select useful data from Neighbor-on-Path input 
	NoP_data nop_tmp = NoP_data_in[directions[i]].read();

	// store the score of node in the direction[i]
	score.push_back(NoPScore(nop_tmp, next_candidate_channels));
    }

    // check for direction with higher score
    //int max_direction = directions[0];
    int max = score[0];
    for (unsigned int i = 0; i < directions.size(); i++) {
	if (score[i] > max) {
	//    max_direction = directions[i];
	    max = score[i];
	}
    }

    // if multiple direction have the same score = max, choose randomly.

    vector < int >equivalent_directions;

    for (unsigned int i = 0; i < directions.size(); i++)
	if (score[i] == max)
	    equivalent_directions.push_back(directions[i]);

    direction_selected =
	equivalent_directions[rand() % equivalent_directions.size()];

    return direction_selected;
}

int Router::selectionBufferLevel(const vector < int >&directions)
{
    vector < int >best_dirs;
    int max_free_slots = 0;
    for (unsigned int i = 0; i < directions.size(); i++) {
	int free_slots = free_slots_neighbor[directions[i]].read();
	bool available = reservation_table.isAvailable(directions[i]);
	if (available) {
	    if (free_slots > max_free_slots) {
		max_free_slots = free_slots;
		best_dirs.clear();
		best_dirs.push_back(directions[i]);
	    } else if (free_slots == max_free_slots)
		best_dirs.push_back(directions[i]);
	}
    }

    if (best_dirs.size())
	return (best_dirs[rand() % best_dirs.size()]);
    else
	return (directions[rand() % directions.size()]);

    //-------------------------
    // TODO: unfair if multiple directions have same buffer level
    // TODO: to check when both available
//   unsigned int max_free_slots = 0;
//   int direction_choosen = NOT_VALID;

//   for (unsigned int i=0;i<directions.size();i++)
//     {
//       int free_slots = free_slots_neighbor[directions[i]].read();
//       if ((free_slots >= max_free_slots) &&
//        (reservation_table.isAvailable(directions[i])))
//      {
//        direction_choosen = directions[i];
//        max_free_slots = free_slots;
//      }
//     }

//   // No available channel 
//   if (direction_choosen==NOT_VALID)
//     direction_choosen = directions[rand() % directions.size()]; 

//   if(GlobalParams::verbose_mode>VERBOSE_OFF)
//     {
//       ChannelStatus tmp;

//       LOG << "SELECTION between: " << endl;
//       for (unsigned int i=0;i<directions.size();i++)
//      {
//        tmp.free_slots = free_slots_neighbor[directions[i]].read();
//        tmp.available = (reservation_table.isAvailable(directions[i]));
//        cout << "    -> direction " << directions[i] << ", channel status: " << tmp << endl;
//      }
//       cout << " direction choosen: " << direction_choosen << endl;
//     }

//   assert(direction_choosen>=0);
//   return direction_choosen;
}

int Router::selectionRandom(const vector < int >&directions)
{
    return directions[rand() % directions.size()];
}

int Router::selectionFunction(const vector < int >&directions,
				   const RouteData & route_data)
{
    // not so elegant but fast escape ;)
    if (directions.size() == 1)
	return directions[0];

    stats.power.Selection();

    switch (GlobalParams::selection_strategy) {
    case SEL_RANDOM:
	return selectionRandom(directions);
    case SEL_BUFFER_LEVEL:
	return selectionBufferLevel(directions);
    case SEL_NOP:
	return selectionNoP(directions, route_data);
    default:
	assert(false);
    }

    return 0;
}

void Router::configure(const int _id,
			    const double _warm_up_time,
			    const unsigned int _max_buffer_size,
			    GlobalRoutingTable & grt)
{
    local_id = _id;
    stats.configure(_id, _warm_up_time);

    start_from_port = DIRECTION_LOCAL;

    if (grt.isValid())
	routing_table.configure(grt, _id);

    for (int i = 0; i < DIRECTIONS + 2; i++)
	buffer[i].SetMaxBufferSize(_max_buffer_size);

    int row = _id / GlobalParams::mesh_dim_x;
    int col = _id % GlobalParams::mesh_dim_x;
    if (row == 0)
      buffer[DIRECTION_NORTH].Disable();
    if (row == GlobalParams::mesh_dim_y-1)
      buffer[DIRECTION_SOUTH].Disable();
    if (col == 0)
      buffer[DIRECTION_WEST].Disable();
    if (col == GlobalParams::mesh_dim_x-1)
      buffer[DIRECTION_EAST].Disable();
}

unsigned long Router::getRoutedFlits()
{
    return routed_flits;
}

unsigned int Router::getFlitsCount()
{
    unsigned count = 0;

    for (int i = 0; i < DIRECTIONS + 2; i++)
	count += buffer[i].Size();

    return count;
}

double Router::getPower()
{
    return stats.power.getPower();
}

int Router::reflexDirection(int direction) const
{
    if (direction == DIRECTION_NORTH)
	return DIRECTION_SOUTH;
    if (direction == DIRECTION_EAST)
	return DIRECTION_WEST;
    if (direction == DIRECTION_WEST)
	return DIRECTION_EAST;
    if (direction == DIRECTION_SOUTH)
	return DIRECTION_NORTH;

    // you shouldn't be here
    assert(false);
    return NOT_VALID;
}

int Router::getNeighborId(int _id, int direction) const
{
    Coord my_coord = id2Coord(_id);

    switch (direction) {
    case DIRECTION_NORTH:
	if (my_coord.y == 0)
	    return NOT_VALID;
	my_coord.y--;
	break;
    case DIRECTION_SOUTH:
	if (my_coord.y == GlobalParams::mesh_dim_y - 1)
	    return NOT_VALID;
	my_coord.y++;
	break;
    case DIRECTION_EAST:
	if (my_coord.x == GlobalParams::mesh_dim_x - 1)
	    return NOT_VALID;
	my_coord.x++;
	break;
    case DIRECTION_WEST:
	if (my_coord.x == 0)
	    return NOT_VALID;
	my_coord.x--;
	break;
    default:
	LOG << "Direction not valid : " << direction;
	assert(false);
    }

    int neighbor_id = coord2Id(my_coord);

    return neighbor_id;
}

bool Router::inCongestion()
{
    for (int i = 0; i < DIRECTIONS; i++) {
	int flits =
	    GlobalParams::buffer_depth - free_slots_neighbor[i];
	if (flits >
	    (int) (GlobalParams::buffer_depth *
		   GlobalParams::dyad_threshold))
	    return true;
    }

    return false;
}

void Router::ShowBuffersStats(std::ostream & out)
{
  for (int i=0; i<DIRECTIONS+2; i++)
    buffer[i].ShowStats(out);
}
