#include <assert.h>
#include <stdio.h>
#include <omp.h>
#include <stdlib.h>

#define NUM_PROCS 4
#define CACHE_SIZE 4
#define MEM_SIZE 16
#define MSG_BUFFER_SIZE 256
#define MAX_INSTR_NUM 32

typedef unsigned char byte;

typedef enum { MODIFIED, EXCLUSIVE, SHARED, INVALID } cacheLineState;

// In addition to cache line states, each directory entry also has a state
// EM ( exclusive or modified ) : memory block is in only one cache.
//                  When a memory block in a cache is written to, it would have
//                  resulted in cache hit and transition from EXCLUSIVE to MODIFIED.
//                  Main memory / directory has no way of knowing of this transition.
//                  Hence, this state only cares that the memory block is in a single
//                  cache, and not whether its in EXCLUSIVE or MODIFIED.
// S ( shared )     : memory block is in multiple caches
// U ( unowned )    : memory block is not in any cache
typedef enum { EM, S, U } directoryEntryState;

typedef enum { 
    READ_REQUEST,       // requesting node sends to home node on a read miss 
    WRITE_REQUEST,      // requesting node sends to home node on a write miss 
    REPLY_RD,           // home node replies with data to requestor for read request
    REPLY_WR,           // home node replies to requestor for write request
    REPLY_ID,           // home node replies with IDs of sharers to requestor
    INV,                // owner node asks sharers to invalidate
    UPGRADE,            // owner node asks home node to change state to EM
    WRITEBACK_INV,      // home node asks owner node to flush and change to INVALID
    WRITEBACK_INT,      // home node asks owner node to flush and change to SHARED
    FLUSH,              // owner flushes data to home + requestor
    FLUSH_INVACK,       // flush, piggybacking an InvAck message
    EVICT_SHARED,       // handle cache replacement of a shared cache line
    EVICT_MODIFIED      // handle cache replacement of a modified cache line
} transactionType;

// We will create our own address space which will be of size 1 byte
// LSB 4 bits indicate the location in memory
// MSB 4 bits indicate the processor it is present in.
// For example, 0x36 means the memory block at index 6 in the 3rd processor
typedef struct instruction {
    byte type;      // 'R' for read, 'W' for write
    byte address;
    byte value;     // used only for write operations
} instruction;

typedef struct cacheLine {
    byte address;           // this is the address in memory
    byte value;             // this is the value stored in cached memory
    cacheLineState state;   // state for you to implement MESI protocol
} cacheLine;

typedef struct directoryEntry {
    byte bitVector;         // each bit indicates whether that processor has this
                            // memory block in its cache
    directoryEntryState state;
} directoryEntry;

// Note that each message will contain values only in the fields which are relevant 
// to the transactionType
typedef struct message {
    transactionType type;
    int sender;          // thread id that sent the message
    byte address;        // memory block address
    byte value;          // value in memory / cache
    byte bitVector;      // ids of sharer nodes
    int secondReceiver;  // used when you need to send a message to 2 nodes, where
                         // 1 node id is in the sender field
    directoryEntryState dirState;   // directory entry state of the memory block
} message;

typedef struct messageBuffer {
    message queue[ MSG_BUFFER_SIZE ];
    // a circular queue message buffer
    int head;
    int tail;
    int count;          // store total number of messages processed by the node
} messageBuffer;

typedef struct processorNode {
    cacheLine cache[ CACHE_SIZE ];
    byte memory[ MEM_SIZE ];
    directoryEntry directory[ MEM_SIZE ];
    instruction instructions[ MAX_INSTR_NUM ];
    int instructionCount;
} processorNode;

void initializeProcessor( int threadId, processorNode *node, char *dirName );
void sendMessage( int receiver, message msg );  // IMPLEMENT
void handleCacheReplacement( int sender, cacheLine oldCacheLine );  // IMPLEMENT
void printProcessorState( int processorId, processorNode node );

messageBuffer messageBuffers[ NUM_PROCS ];
// IMPLEMENT
// Create locks to ensure thread-safe access to each processor's message buffer.
omp_lock_t msgBufferLocks[ NUM_PROCS ];


int main( int argc, char * argv[] ) {
    if (argc < 2) {
        fprintf( stderr, "Usage: %s <test_directory>\n", argv[0] );
        return EXIT_FAILURE;
    }
    char *dirName = argv[1];
    
    // IMPLEMENT
    // set number of threads to NUM_PROCS

    omp_set_num_threads( NUM_PROCS );

    for ( int i = 0; i < NUM_PROCS; i++ ) {
        messageBuffers[ i ].count = 0;
        messageBuffers[ i ].head = 0;
        messageBuffers[ i ].tail = 0;
        // IMPLEMENT
        // initialize the locks in msgBufferLocks
        omp_init_lock( &msgBufferLocks[ i ] );
    }
    processorNode node;

    // IMPLEMENT
    // Create the omp parallel region with an appropriate data environment
    // Need to figure out what data environnemnt
    #pragma omp parallel private(node)
    {
        int threadId = omp_get_thread_num();
        initializeProcessor( threadId, &node, dirName );
        // IMPLEMENT
        // wait for all processors to complete initialization before proceeding
        #pragma omp barrier
        
        message msg;
        message msgReply;
        instruction instr;
        int instructionIdx = -1;
        int printProcState = 1;         // control how many times to dump processor
        byte waitingForReply = 0;       // if a processor is waiting for a reply
                                        // then dont proceed to next instruction
                                        // as current instruction has not finished
        while ( 1 ) {
            // Process all messages in message queue first
            while ( 
                messageBuffers[ threadId ].count > 0 &&
                messageBuffers[ threadId ].head != messageBuffers[ threadId ].tail
            ) {
                if ( printProcState == 0 ) {
                    printProcState++;
                }
                int head = messageBuffers[ threadId ].head;
                msg = messageBuffers[ threadId ].queue[ head ];
                messageBuffers[ threadId ].head = ( head + 1 ) % MSG_BUFFER_SIZE;

                #ifdef DEBUG_MSG
                printf( "Processor %d msg from: %d, type: %s, address: 0x%02X\n",
                        threadId, msg.sender, 
                        (msg.type == READ_REQUEST) ? "READ_REQUEST" :
                        (msg.type == WRITE_REQUEST) ? "WRITE_REQUEST" :
                        (msg.type == REPLY_RD) ? "REPLY_RD" :
                        (msg.type == REPLY_WR) ? "REPLY_WR" :
                        (msg.type == REPLY_ID) ? "REPLY_ID" :
                        (msg.type == INV) ? "INV" :
                        (msg.type == UPGRADE) ? "UPGRADE" :
                        (msg.type == WRITEBACK_INV) ? "WRITEBACK_INV" :
                        (msg.type == WRITEBACK_INT) ? "WRITEBACK_INT" :
                        (msg.type == FLUSH) ? "FLUSH" :
                        (msg.type == FLUSH_INVACK) ? "FLUSH_INVACK" :
                        (msg.type == EVICT_SHARED) ? "EVICT_SHARED" :
                        (msg.type == EVICT_MODIFIED) ? "EVICT_MODIFIED" :
                        "UNKNOWN",
                        msg.address );
                #endif /* ifdef DEBUG */

                // IMPLEMENT
                // extract procNodeAddr and memBlockAddr from message address
                byte procNodeAddr = ( msg.address & 0xF0 ) >> 4;
                byte memBlockAddr = (msg.address & 0x0F);
                byte cacheIndex = memBlockAddr % CACHE_SIZE;

                switch ( msg.type ) {
                    case READ_REQUEST:
                        // IMPLEMENT
                        // This is in the home node
                        // If directory state is,
                        // U: update directory and send value using REPLY_RD
                        // S: update directory and send value using REPLY_RD
                        // EM: forward request to the current owner node for
                        //     writeback intervention using WRITEBACK_INT

                        switch (node.directory[ memBlockAddr ].state) {
                            case U:
                                // update directory State
                                node.directory[ memBlockAddr ].state = EM;
                                node.directory[ memBlockAddr ].bitVector = 1 << msg.sender;

                                // send value to requesting node
                                msgReply = (message) {
                                    .type = REPLY_RD,
                                    .sender = threadId,
                                    .address = msg.address,
                                    .value = node.memory[ memBlockAddr],
                                    .dirState = EM
                                };
                                sendMessage( procNodeAddr, msgReply );
                                break;

                            case S:
                                // update dierectory state
                                node.directory[ memBlockAddr ].bitVector |= 1 << msg.sender;

                                // send value to requesting node
                                msgReply = (message) {
                                    .type = REPLY_RD,
                                    .sender = threadId,
                                    .address = msg.address,
                                    .value = node.memory[ memBlockAddr ],
                                    .dirState = S
                                };

                                sendMessage(procNodeAddr, msgReply);
                                break;

                            case EM:
                                int currOwner = __builtin_ctz(node.directory[ memBlockAddr ].bitVector);

                                msgReply = (message) {
                                    .type = WRITEBACK_INT,
                                    .sender = threadId,
                                    .address = msg.address,
                                    .secondReceiver = msg.sender
                                };
                                sendMessage( currOwner, msgReply );
                                break;
                            default:
                                printf("Invalid directory state\n");
                                break;

                        }
                        
                        break;

                    case REPLY_RD:
                        // IMPLEMENT
                        // This is in the requesting node
                        // If some other memory block was in cacheline, you need to
                        // handle cache replacement
                        // Read in the memory block sent in the message to cache
                        // Handle state of the memory block appropriately

                        // check if cache miss
                        if (node.cache[cacheIndex].address != msg.address && node.cache[cacheIndex].state != INVALID) {
                            handleCacheReplacement(threadId, node.cache[cacheIndex]);
                        }

                        // update cache
                        node.cache[cacheIndex].address = msg.address;
                        node.cache[cacheIndex].value = msg.value;
                        node.cache[cacheIndex].state =(msg.dirState == EM) ? EXCLUSIVE : SHARED;

                        // set waitingForReply to 0
                        waitingForReply = 0;

                        break;

                    case WRITEBACK_INT:
                        // IMPLEMENT
                        // This is in old owner node which contains a cache line in 
                        // MODIFIED state
                        // Flush this value to the home node and the requesting node
                        // using FLUSH
                        // Change cacheline state to SHARED
                        // If home node is the requesting node, avoid sending FLUSH
                        // twice

                        msgReply = (message) {
                            .type = FLUSH,
                            .sender = threadId,
                            .address = msg.address,
                            .value = node.cache[cacheIndex].value,
                        };

                        if (procNodeAddr != msg.sender){
                            sendMessage(msg.sender, msgReply);
                        }

                        sendMessage(procNodeAddr, msgReply);

                        node.cache[cacheIndex].state = SHARED;

                        break;

                    case FLUSH:
                        // IMPLEMENT
                        // If in home node,
                        // update directory state and bitvector appropriately
                        // update memory value
                        //
                        // If in requesting node, load block into cache
                        // If some other memory block was in cacheline, you need to
                        // handle cache replacement
                        //
                        // IMPORTANT: there can be cases where home node is same as
                        // requesting node, which need to be handled appropriately

                        //Handle Homenode
                        if (threadId == procNodeAddr){
                            node.memory[memBlockAddr] = msg.value;
                            node.directory[memBlockAddr].state = S;
                            node.directory[memBlockAddr].bitVector |=(1 << msg.sender);
                            if (msg.secondReceiver >= 0 && msg.secondReceiver < NUM_PROCS)  {
                                node.directory[memBlockAddr].bitVector |= (1 << msg.secondReceiver);
                            }
                        }else{
                        //Handle sender node
                            if (node.cache[cacheIndex].state != INVALID && node.cache[cacheIndex].address != msg.address){
                                handleCacheReplacement(threadId,node.cache[cacheIndex]);
                            }

                            node.cache[cacheIndex].value = msg.value;
                            node.cache[cacheIndex].address = msg.address; // In case INVALID
                            node.cache[cacheIndex].state = SHARED;
                            waitingForReply = 0;
                        }

                        break;

                    case UPGRADE:
                        // IMPLEMENT
                        // This is in the home node
                        // The requesting node had a write hit on a SHARED cacheline
                        // Send list of sharers to requesting node using REPLY_ID
                        // Update directory state to EM, and bit vector to only have
                        // the requesting node set
                        // IMPORTANT: Do not include the requesting node in the
                        // sharers list


                        //Create a sharer's list
                        byte sharersList = node.directory[memBlockAddr].bitVector & (~(1 << msg.sender));
                        msgReply = (message){
                            .type = REPLY_ID,
                            .sender = threadId,
                            .address = msg.address,
                            .bitVector = sharersList,
                        };

                        sendMessage(msg.sender, msgReply);

                        node.directory[memBlockAddr].state = EM;
                        node.directory[memBlockAddr].bitVector = (1 << msg.sender);

                        break;

                    case REPLY_ID:
                        // IMPLEMENT
                        // This is in the requesting ( new owner ) node
                        // The owner node recevied the sharers list from home node
                        // Invalidate all sharers' entries using INV
                        // Handle cache replacement if needed, and load the block
                        // into the cacheline
                        // NOTE: Ideally, we should update the owner node cache line
                        // after we receive INV_ACK from every sharer, but for that
                        // we will have to keep track of all the INV_ACKs.
                        // Instead, we will assume that INV does not fail.
                        byte sharedNodes = msg.bitVector;
                        for (int i = 0; i < NUM_PROCS; i++){
                            if (sharedNodes & (1 << i)) {
                                msgReply = (message) {
                                    .type = INV,
                                    .sender = threadId,
                                    .address = msg.address,
                                };
                                sendMessage(i, msgReply);
                            }
                        }

                        // Replace cache if needed
                        if (node.cache[cacheIndex].state != INVALID && node.cache[cacheIndex].address != msg.address){
                            handleCacheReplacement(threadId, node.cache[cacheIndex]);
                        }

                        // Update cache
                        node.cache[cacheIndex].address = msg.address;
                        node.cache[cacheIndex].state = MODIFIED;
                        node.cache[cacheIndex].value = instr.value; //Flagging this this could jsut change to msg.value since im passing it with the message
                        
                        waitingForReply = 0;
                        break;

                    case INV:
                        // IMPLEMENT
                        // This is in the sharer node
                        // Invalidate the cache entry for memory block
                        // If the cache no longer has the memory block ( replaced by
                        // a different block ), then do nothing

                        if (node.cache[cacheIndex].address == msg.address){
                            node.cache[cacheIndex].state = INVALID;
                        }

                        break;

                    case WRITE_REQUEST:
                        // IMPLEMENT
                        // This is in the home node
                        // Write miss occured in requesting node
                        // If the directory state is,
                        // U:   no cache contains this memory block, and requesting
                        //      node directly becomes the owner node, use REPLY_WR
                        // S:   other caches contain this memory block in clean state
                        //      which have to be invalidated
                        //      send sharers list to new owner node using REPLY_ID
                        //      update directory
                        // EM:  one other cache contains this memory block, which
                        //      can be in EXCLUSIVE or MODIFIED
                        //      send WRITEBACK_INV to the old owner, to flush value
                        //      into memory and invalidate cacheline
                        #ifdef DEBUG_MSG
                        printf("Processor %d: WRITE_REQUEST: address=0x%02X, value=%hhu\n", threadId, msg.address, msg.value);
                        #endif
                        switch (node.directory[memBlockAddr].state){
                            case U:
                                
                                //No cache has this memory block, make sender Exclusive Owner
                                node.directory[memBlockAddr].state = EM;
                                node.directory[memBlockAddr].bitVector = 1 << msg.sender;

                                //Send ack
                                msgReply = (message){
                                    .type = REPLY_WR,
                                    .sender = threadId,
                                    .address = msg.address,
                                    .value = msg.value,
                                };
                                sendMessage(msg.sender, msgReply);
                                break;
                            case S:

                                //Send REPLY_ID
                                msgReply = (message) {
                                    .type = REPLY_ID,
                                    .sender = threadId,
                                    .address = msg.address,
                                    .bitVector = node.directory[memBlockAddr].bitVector & (~(1 << msg.sender)),
                                    .value = node.memory[memBlockAddr],
                                };
                                sendMessage(msg.sender, msgReply);

                                //Update Dir
                                node.directory[memBlockAddr].state = EM;
                                node.directory[memBlockAddr].bitVector = 1 << msg.sender;
                                break;

                            case EM:
                                //Send WRITEBACK_INV
                                int currOwner = __builtin_ctz(node.directory[memBlockAddr].bitVector);
                                msgReply = (message){
                                    .type = WRITEBACK_INV,
                                    .sender = threadId,
                                    .address = msg.address,
                                    .secondReceiver = msg.sender,
                                };
                                sendMessage(currOwner, msgReply);
                                break;
                        }

                        break;

                    case REPLY_WR:
                        // IMPLEMENT
                        // This is in the requesting ( new owner ) node
                        // Handle cache replacement if needed, and load the memory
                        // block into cache
                        
                        if (node.cache[cacheIndex].state != INVALID && node.cache[cacheIndex].address != msg.address){
                            handleCacheReplacement(threadId, node.cache[cacheIndex]);
                        }
                        #ifdef DEBUG_MSG
                        printf("Processor %d: REPLY_WR: address=0x%02X, value=%hhu\n", threadId, msg.address, msg.value);
                        #endif
                        //Update CacheLine 
                        node.cache[cacheIndex].address = msg.address;
                        node.cache[cacheIndex].value = msg.value;
                        node.cache[cacheIndex].state = MODIFIED;
                        waitingForReply = 0;
                        break;

                    case WRITEBACK_INV:
                        // IMPLEMENT
                        // This is in the old owner node
                        // Flush the currrent value to home node using FLUSH_INVACK
                        // Send an ack to the requesting node which will become the
                        // new owner node
                        // If home node is the new owner node, dont send twice
                        // Invalidate the cacheline

                        // Makes sense only to flush if hasnt been replaced?
                        if (node.cache[cacheIndex].address == msg.address && node.cache[cacheIndex].state != INVALID)
                            {
                                msgReply = (message){
                                    .type = FLUSH_INVACK,
                                    .sender = threadId,
                                    .address = msg.address,
                                    .value = node.cache[cacheIndex].value,
                                };

                                sendMessage(msg.sender, msgReply);

                                if (msg.secondReceiver != msg.sender){
                                    sendMessage(msg.secondReceiver, msgReply);
                                }

                                node.cache[cacheIndex].state = INVALID;
                            }
                        break;

                    case FLUSH_INVACK:
                        // IMPLEMENT
                        // If in home node, update directory and memory
                        // The bit vector should have only the new owner node set
                        // Flush the value from the old owner to memory
                        //
                        // If in requesting node, handle cache replacement if needed,
                        // and load block into cache

                        if (threadId == procNodeAddr){
                            //home sweet home
                            node.memory[memBlockAddr] = msg.value;
                            node.directory[memBlockAddr].state = EM;
                            node.directory[memBlockAddr].bitVector = 1 << msg.sender; //Im being lazy and not checking bounds of sender please work
                        }else{
                            //Req node
                            if (node.cache[cacheIndex].state != INVALID && node.cache[cacheIndex].address != msg.address){
                                handleCacheReplacement(threadId, node.cache[cacheIndex]);
                            }

                            node.cache[cacheIndex].address = msg.address;
                            node.cache[cacheIndex].value = msg.value;
                            node.cache[cacheIndex].state = MODIFIED;
                            waitingForReply = 0;
                        }
                        break;
                    
                    case EVICT_SHARED:
                        // IMPLEMENT
                        // If in home node,
                        // Requesting node evicted a cacheline which was in SHARED
                        // Remove the old node from bitvector,
                        // if no more sharers exist, change directory state to U
                        // if only one sharer exist, change directory state to EM
                        // Inform the remaining sharer ( which will become owner ) to
                        // change from SHARED to EXCLUSIVE using EVICT_SHARED
                        //
                        // If in remaining sharer ( new owner ), update cacheline
                        // from SHARED to EXCLUSIVE

                        if (threadId == procNodeAddr){
                            node.directory[memBlockAddr].bitVector &= ~(1 << msg.sender);


                            if (node.directory[memBlockAddr].bitVector == 0){
                                //No more sharing yipee
                                node.directory[memBlockAddr].state = U;
                            }else if (__builtin_popcount(node.directory[memBlockAddr].bitVector) == 1){
                                //owning exclusive memory in this economy?!
                                node.directory[memBlockAddr].state = EM;
                                //Need to figure this out, how can i evict everyone ?_?
                                msgReply = (message){
                                    .type = EVICT_SHARED,
                                    .sender = threadId,
                                    .address = msg.address,
                                    .value = node.memory[memBlockAddr],
                                };
                                sendMessage(__builtin_ctz(node.directory[memBlockAddr].bitVector), msgReply);
                            } //else do nothing
                        }else{

                            node.cache[cacheIndex].state = EXCLUSIVE;}
                        break;

                    case EVICT_MODIFIED:
                        // IMPLEMENT
                        // This is in home node,
                        // Requesting node evicted a cacheline which was in MODIFIED
                        // Flush value to memory
                        // Remove the old node from bitvector HINT: since it was in
                        // modified state, not other node should have had that
                        // memory block in a valid state its cache

                        node.memory[memBlockAddr] = msg.value;
                        node.directory[memBlockAddr].bitVector  = 0;
                        node.directory[memBlockAddr].state = U;

                        break;
                }
                
            }
            
            // Check if we are waiting for a reply message
            // if yes, then we have to complete the previous instruction before
            // moving on to the next
            if ( waitingForReply > 0 ) {
                continue;
            }

            // Process an instruction
            if ( instructionIdx < node.instructionCount - 1 ) {
                instructionIdx++;
            } else {
                if ( printProcState > 0 ) {
                    printProcessorState( threadId, node );
                    printProcState--;
                }
                // even though there are no more instructions, this processor might
                // still need to react to new transaction messages
                //
                // once all the processors are done printing and appear to have no
                // more network transactions, please terminate the program by sending
                // a SIGINT ( CTRL+C )
                continue;
            }
            instr = node.instructions[ instructionIdx ];

            #ifdef DEBUG_INSTR
            printf( "Processor %d: instr type=%c, address=0x%02X, value=%hhu\n",
                    threadId, instr.type, instr.address, instr.value );
            #endif

            // IMPLEMENT
            // Extract the home node's address and memory block index from
            // instruction address
            byte procNodeAddr = ( instr.address & 0xF0 ) >> 4;
            byte memBlockAddr = instr.address & 0x0F;
            byte cacheIndex = memBlockAddr % CACHE_SIZE;


          if ( instr.type == 'R' ) {
                // IMPLEMENT
                // check if memory block is present in cache
                //
                // if cache hit and the cacheline state is not invalid, then use
                // that value. no transaction/transition takes place so no work.
                //
                // if cacheline is invalid, or memory block is not present, it is
                // treated as a read miss
                // send a READ_REQUEST to home node on a read miss
                if (node.cache[cacheIndex].address == instr.address && node.cache[cacheIndex].state != INVALID){
                    //Cache hit
                    //No transaction needed
                }else{
                    //Cache miss, need to send READ_REQ to home node
                    msg = (message) {
                        .type = READ_REQUEST,
                        .sender = threadId,
                        .address = instr.address,
                    };
                    sendMessage(procNodeAddr, msg);
                    waitingForReply = 1;

                }
            } else {
                // IMPLEMENT
                // check if memory block is present in cache
                //
                // if cache hit and cacheline state is not invalid, then it is a
                // write hit
                // if modified or exclusive, update cache directly as only this node
                // contains the memory block, no network transactions required
                // if shared, other nodes contain this memory block, request home
                // node to send list of sharers. This node will have to send an
                // UPGRADE to home node to promote directory from S to EM, and also
                // invalidate the entries in the sharers
                //
                // if cache miss or cacheline state is invalid, then it is a write
                // miss
                // send a WRITE_REQUEST to home node on a write miss

                if (node.cache[cacheIndex].address == instr.address && node.cache[cacheIndex].state != INVALID){
                    //Write Hit
                    if (node.cache[cacheIndex].state == EXCLUSIVE || node.cache[cacheIndex].state == MODIFIED){
                        //Update cache Since its EM
                        node.cache[cacheIndex].value = instr.value;
                        node.cache[cacheIndex].state = MODIFIED;
                    }else{
                        //Shared Cache
                        assert(node.cache[cacheIndex].state == SHARED); //Doing this so that i can debug better cuz something is wrong for sure:) 

                        msg = (message) {
                            .type = UPGRADE,
                            .sender = threadId,
                            .address = instr.address,
                            .value = instr.value,
                        };
                        sendMessage(procNodeAddr, msg);
                        waitingForReply = 1;
                    }
                }else{
                    //Write Miss
                    msg = (message) {
                        .type = WRITE_REQUEST,
                        .sender = threadId,
                        .address = instr.address,
                        .value = instr.value,
                    };
                    sendMessage(procNodeAddr, msg);
                    waitingForReply = 1;
                }
            }

        }
    }
}

void sendMessage( int receiver, message msg ) {
    // IMPLEMENT
    // Ensure thread safety while adding a message to the receiver's buffer
    // Manage buffer indices correctly to maintain a circular queue structure

    //Lock the buffer
    omp_set_lock(&msgBufferLocks[receiver]);

    // Check for overflow
    if (messageBuffers[receiver].count == MSG_BUFFER_SIZE) {
        fprintf(stderr, "Error: Message buffer overflow for processor %d\n", receiver);
        omp_unset_lock(&msgBufferLocks[receiver]);
        return;
    }

    // Add message to buffer
    messageBuffers[receiver].queue[messageBuffers[receiver].tail] = msg;
    messageBuffers[receiver].tail = (messageBuffers[receiver].tail + 1) % MSG_BUFFER_SIZE;
    messageBuffers[receiver].count++;

    // Unlock the buffer
    omp_unset_lock(&msgBufferLocks[receiver]);
    
}

void handleCacheReplacement( int sender, cacheLine oldCacheLine ) {
    // IMPLEMENT
    // Notify the home node before a cacheline gets replaced
    // Extract the home node's address and memory block index from cacheline address
    byte memBlockAddr = oldCacheLine.address & 0x0F;
    byte procNodeAddr = ( oldCacheLine.address & 0xF0 ) >> 4;
    message msgRepl;
    
    switch ( oldCacheLine.state ) {
        case EXCLUSIVE:
        case SHARED:
            // IMPLEMENT
            // If cache line was shared or exclusive, inform home node about the
            // eviction
            msgRepl = (message) {
                .type = EVICT_SHARED,
                .sender = sender,
                .address = oldCacheLine.address,
            };

            sendMessage(procNodeAddr, msgRepl);
            break;

        case MODIFIED:
            // IMPLEMENT
            // If cache line was modified, send updated value to home node 
            // so that memory can be updated before eviction

            //How do i send the updated value when the updated value isnt being sent to the function? :')
            msgRepl = (message){
                .type = EVICT_MODIFIED,
                .sender = sender,
                .address = oldCacheLine.address,
                .value = oldCacheLine.value,
            };
            sendMessage(procNodeAddr, msgRepl);
            break;
        case INVALID:
            // No action required for INVALID state
            break;
    };
}

void initializeProcessor( int threadId, processorNode *node, char *dirName ) {
    // IMPORTANT: DO NOT MODIFY
    for ( int i = 0; i < MEM_SIZE; i++ ) {
        node->memory[ i ] = 20 * threadId + i;  // some initial value to mem block
        node->directory[ i ].bitVector = 0;     // no cache has this block at start
        node->directory[ i ].state = U;         // this block is in Unowned state
    }

    for ( int i = 0; i < CACHE_SIZE; i++ ) {
        node->cache[ i ].address = 0xFF;        // this address is invalid as we can
                                                // have a maximum of 8 nodes in the 
                                                // current implementation
        node->cache[ i ].value = 0;
        node->cache[ i ].state = INVALID;       // all cache lines are invalid
    }

    // read and parse instructions from core_<threadId>.txt
    char filename[ 128 ];
    snprintf(filename, sizeof(filename), "tests/%s/core_%d.txt", dirName, threadId);
    FILE *file = fopen( filename, "r" );
    if ( !file ) {
        fprintf( stderr, "Error: count not open file %s\n", filename );
        exit( EXIT_FAILURE );
    }

    char line[ 20 ];
    node->instructionCount = 0;
    while ( fgets( line, sizeof( line ), file ) &&
            node->instructionCount < MAX_INSTR_NUM ) {
        if ( line[ 0 ] == 'R' && line[ 1 ] == 'D' ) {
            sscanf( line, "RD %hhx",
                    &node->instructions[ node->instructionCount ].address );
            node->instructions[ node->instructionCount ].type = 'R';
            node->instructions[ node->instructionCount ].value = 0;
        } else if ( line[ 0 ] == 'W' && line[ 1 ] == 'R' ) {
            sscanf( line, "WR %hhx %hhu",
                    &node->instructions[ node->instructionCount ].address,
                    &node->instructions[ node->instructionCount ].value );
            node->instructions[ node->instructionCount ].type = 'W';
        }
        node->instructionCount++;
    }

    fclose( file );
    printf( "Processor %d initialized\n", threadId );
}

void printProcessorState(int processorId, processorNode node) {
    // IMPORTANT: DO NOT MODIFY
    static const char *cacheStateStr[] = { "MODIFIED", "EXCLUSIVE", "SHARED",
                                           "INVALID" };
    static const char *dirStateStr[] = { "EM", "S", "U" };

    char filename[32];
    snprintf(filename, sizeof(filename), "core_%d_output.txt", processorId);

    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not open file %s\n", filename);
        return;
    }

    fprintf(file, "=======================================\n");
    fprintf(file, " Processor Node: %d\n", processorId);
    fprintf(file, "=======================================\n\n");

    // Print memory state
    fprintf(file, "-------- Memory State --------\n");
    fprintf(file, "| Index | Address |   Value  |\n");
    fprintf(file, "|----------------------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        fprintf(file, "|  %3d  |  0x%02X   |  %5d   |\n", i, (processorId << 4) + i,
                node.memory[i]);
    }
    fprintf(file, "------------------------------\n\n");

    // Print directory state
    fprintf(file, "------------ Directory State ---------------\n");
    fprintf(file, "| Index | Address | State |    BitVector   |\n");
    fprintf(file, "|------------------------------------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        fprintf(file, "|  %3d  |  0x%02X   |  %2s   |   0x%08B   |\n",
                i, (processorId << 4) + i, dirStateStr[node.directory[i].state],
                node.directory[i].bitVector);
    }
    fprintf(file, "--------------------------------------------\n\n");
    
    // Print cache state
    fprintf(file, "------------ Cache State ----------------\n");
    fprintf(file, "| Index | Address | Value |    State    |\n");
    fprintf(file, "|---------------------------------------|\n");
    for (int i = 0; i < CACHE_SIZE; i++) {
        fprintf(file, "|  %3d  |  0x%02X   |  %3d  |  %8s \t|\n",
               i, node.cache[i].address, node.cache[i].value,
               cacheStateStr[node.cache[i].state]);
    }
    fprintf(file, "----------------------------------------\n\n");

    fclose(file);
}
