#include <memory.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include "page.h"
#include "buf.h"

#define ASSERT(c)  { if (!(c)) { \
		       cerr << "At line " << __LINE__ << ":" << endl << "  "; \
                       cerr << "This condition should hold: " #c << endl; \
                       exit(1); \
		     } \
                   }

//----------------------------------------
// Constructor of the class BufMgr
//----------------------------------------

BufMgr::BufMgr(const int bufs)
{
    numBufs = bufs;

    bufTable = new BufDesc[bufs];
    memset(bufTable, 0, bufs * sizeof(BufDesc));
    for (int i = 0; i < bufs; i++) 
    {
        bufTable[i].frameNo = i;
        bufTable[i].valid = false;
    }

    bufPool = new Page[bufs];
    memset(bufPool, 0, bufs * sizeof(Page));

    int htsize = ((((int) (bufs * 1.2))*2)/2)+1;
    hashTable = new BufHashTbl (htsize);  // allocate the buffer hash table

    clockHand = bufs - 1;
}


BufMgr::~BufMgr() {

    // flush out all unwritten pages
    for (int i = 0; i < numBufs; i++) 
    {
        BufDesc* tmpbuf = &bufTable[i];
        if (tmpbuf->valid == true && tmpbuf->dirty == true) {

#ifdef DEBUGBUF
            cout << "flushing page " << tmpbuf->pageNo
                 << " from frame " << i << endl;
#endif

            tmpbuf->file->writePage(tmpbuf->pageNo, &(bufPool[i]));
        }
    }

    delete [] bufTable;
    delete [] bufPool;
}


const Status BufMgr::allocBuf(int & frame) 
{

    int iter = 0;
    unsigned int startHand = clockHand;
    //clockHand is initialized to the last index,we move to the first
    advanceClock();

    while(1){
        
        
        //rule1:check valid bit, allocate directly if false
        if(bufTable[clockHand].valid == false){
            
            frame = bufTable[clockHand].frameNo;
            return OK;
        }else{
            //check refBit and pinCnt
            if(bufTable[clockHand].refbit == true){
                bufTable[clockHand].refbit = false;
                advanceClock();
                if(clockHand == startHand){
                iter++;
                if(iter == 2){
                    return BUFFEREXCEEDED;
                }
            }
                continue;
            }
            
            if(bufTable[clockHand].refbit == false && bufTable[clockHand].pinCnt == 0){
                
                if(bufTable[clockHand].dirty == true){
                    
                    //
                    bufTable[clockHand].pinCnt = 0;
                    Status stat = bufTable[clockHand].file->writePage(bufTable[clockHand].pageNo, &bufPool[clockHand]);
                    if(stat != OK){
                        return stat;
                    }
                }
                
                hashTable->remove(bufTable[clockHand].file, bufTable[clockHand].pageNo);
                frame = bufTable[clockHand].frameNo;
                return OK;
            }
            
            advanceClock();

            if(clockHand == startHand){
                iter++;
                if(iter == 2){
                    return BUFFEREXCEEDED;
                }
            }
        }

        

    }

}

	
const Status BufMgr::readPage(File* file, const int PageNo, Page*& page)
{
    int frameNo = 0;
    Status stat = OK;
    if(hashTable->lookup(file, PageNo, frameNo) != HASHNOTFOUND){
        
        
        bufTable[frameNo].refbit = true;
        bufTable[frameNo].pinCnt++;
        page = &bufPool[frameNo];

    }else{
        stat = allocBuf(frameNo);
        if(stat != OK){
            return stat;
        }
        stat = file->readPage(PageNo, &bufPool[frameNo]);
        if(stat != OK){
            return stat;
        }
        

        stat = hashTable->insert(file,PageNo, frameNo);
        if(stat != OK){
            return stat;
        }
        bufTable[frameNo].Set(file, PageNo);
        page = &bufPool[frameNo];
    }

    return stat;
}


const Status BufMgr::unPinPage(File* file, const int PageNo, 
			       const bool dirty) 
{
    int frameNo = 0;
    Status stat = hashTable->lookup(file, PageNo, frameNo);
    
    if(stat != OK){
        return stat;
    }
    if(bufTable[frameNo].pinCnt == 0){
        return PAGENOTPINNED;
    }
    bufTable[frameNo].pinCnt--;
    if(dirty == true){
        bufTable[frameNo].dirty = true;
    }
    

    return OK;



}

const Status BufMgr::allocPage(File* file, int& pageNo, Page*& page) 
{
    int frameNo = 0;
    Status stat = OK;
    stat = file->allocatePage(pageNo);
    if(stat != OK){
        return stat;
    }
    stat = allocBuf(frameNo);
    
    if(stat != OK){
        return stat;
    }
    
    stat = hashTable->insert(file, pageNo, frameNo);
    if(stat != 0){
        return stat;
    }
    
    bufTable[frameNo].Set(file, pageNo);
    page = &bufPool[frameNo];


    return stat;

}

const Status BufMgr::disposePage(File* file, const int pageNo) 
{
    // see if it is in the buffer pool
    Status status = OK;
    int frameNo = 0;
    status = hashTable->lookup(file, pageNo, frameNo);
    if (status == OK)
    {
        // clear the page
        bufTable[frameNo].Clear();
    }
    status = hashTable->remove(file, pageNo);

    // deallocate it in the file
    return file->disposePage(pageNo);
}

const Status BufMgr::flushFile(const File* file) 
{
  Status status;

  for (int i = 0; i < numBufs; i++) {
    BufDesc* tmpbuf = &(bufTable[i]);
    if (tmpbuf->valid == true && tmpbuf->file == file) {

      if (tmpbuf->pinCnt > 0)
	  return PAGEPINNED;

      if (tmpbuf->dirty == true) {
#ifdef DEBUGBUF
	cout << "flushing page " << tmpbuf->pageNo
             << " from frame " << i << endl;
#endif
	if ((status = tmpbuf->file->writePage(tmpbuf->pageNo,
					      &(bufPool[i]))) != OK)
	  return status;

	tmpbuf->dirty = false;
      }

      hashTable->remove(file,tmpbuf->pageNo);

      tmpbuf->file = NULL;
      tmpbuf->pageNo = -1;
      tmpbuf->valid = false;
    }

    else if (tmpbuf->valid == false && tmpbuf->file == file)
      return BADBUFFER;
  }
  
  return OK;
}


void BufMgr::printSelf(void) 
{
    BufDesc* tmpbuf;
  
    cout << endl << "Print buffer...\n";
    for (int i=0; i<numBufs; i++) {
        tmpbuf = &(bufTable[i]);
        cout << i << "\t" << (char*)(&bufPool[i]) 
             << "\tpinCnt: " << tmpbuf->pinCnt;
    
        if (tmpbuf->valid == true)
            cout << "\tvalid\n";
        cout << endl;
    };
}


