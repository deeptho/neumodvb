##############################################################################
## Name:        genFile.py
## Purpose:     saves generated files
## Author:      Alex Thuering
## Created:     2005/01/19
## RCS-ID:      $Id: genFile.py,v 1.3 2014/03/21 21:15:35 ntalex Exp $
## Copyright:   (c) 2005 Alex Thuering
## Notes:       some modules adapted from svgl project
##############################################################################

class genFile:
    def __init__(self, filename):
        self.filename = filename
        self.content = ""
        self.closed=0

    def __del__(self):
        if self.closed==0:
            self.close()

    def close(self):
        if(self.closed==1):
            raise self.filename+' already closed'
        writeit=0
        prevContent = None
        try:
            f = open(self.filename, 'rU')
            prevContent = f.read()
            if(prevContent != self.content):
                #print 'different'
                writeit=1
            else:
                #print self.filename + " unchanged"
                pass
            f.close()
        except IOError:
            #print "can't open"
            writeit=1

        if(writeit==1):
            print(self.filename + " changed")
            if(prevContent):
                f = open(self.filename+'.prev', 'wb')
                f.write(bytes(prevContent, 'UTF-8'))
                f.close()
            f = open(self.filename, 'wb')
            f.write(bytes(self.content, 'UTF-8'))
            
        f.close()
        self.closed=1

    def write(self, s):
        self.content = self.content + s

def gfopen(filename, rw='wb'):
    return genFile(filename)

