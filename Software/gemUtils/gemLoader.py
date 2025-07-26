# -*- coding: utf-8 -*-
"""
Created on Tue Nov 19 21:27:17 2024

@author: rusty
"""

import pathlib


def parseFacetLine(line, wheelIndex = 96, appendTo = None):
    
    def facetParse(lineList, wheelInd = wheelIndex):
        
        facetName = False
        facetIndex = []
        for i, sL in enumerate(lineList):

            if facetName:
                facetIndex[-1]['name'] = sL
                facetName = False
                continue
    
            
            if sL == 'n':
                # Facet name is next
                facetName = True
                continue
            
            if sL == '':
                continue
            
            facetComment = ''
            if sL == 'G':
                # Rest is comments
                facetComment = ' '.join(lineList[(i+1):]).rstrip()
                break
            
            idx = float(sL)

            facetIndex.append({'value' : idx, 
                               'name' : '', 
                               'deg' : 360*(idx/wheelInd), 
                               'frac' : idx/wheelInd})
            
        return facetIndex, facetComment
    
    
    if not(appendTo is None):
        
        facetIndex, facetComment = facetParse(line.split(' '), wheelInd = wheelIndex)
        appendTo['facets'] = appendTo['facets'] + facetIndex
        appendTo['comments'] = appendTo['comments'] + facetComment
        
        return appendTo
    
    else:
    
        if not(line.startswith('a')):
            ValueError('Not a facet line!')
            
        splitLine = line.split(' ')
        
        angle = float(splitLine[1])
        trueForCrown = angle >= 0
        
        depth = float(splitLine[2])
    
        facetIndex, facetComment = facetParse(splitLine[3::], wheelInd = wheelIndex)
        
        

       
            
        facetDict = {'angle' : angle,
                     'isCrown' : trueForCrown,
                     'depth' : depth,
                     'facets' : facetIndex,
                     'nFacets' : len(facetIndex),
                     'comments' : facetComment}
        
        return facetDict
        

def parseAllLines(txt):
        
       facetList = [] 
       doFirstLine = True 
       doFirstTitle = True
       commentLines = []   
       littleTitleLines = []   
       wheelIndex = 0
       foldSymmetry = None
       hasMirrorPlane = None
       refIndex = None
       bigName = ''
       lastWasFacetLine = False
       
       for t in txt:
           
           gemCADver = None
           if doFirstLine:
               gemCADver = float(t.split(' ')[-1])
               doFirstLine = False
               lastWasFacetLine = False

           
           if t.startswith('g'):
               wheelIndex = int(t.split(' ')[1])
               lastWasFacetLine = False
               
           elif t.startswith('y'):
               foldSymmetry = int(t.split(' ')[1])
               hasMirrorPlane = (t.split(' ')[2] == 'y')
               lastWasFacetLine = False
           
           elif t.startswith('I'):
               refIndex = float(t.split(' ')[1])
               lastWasFacetLine = False
               
           elif t.startswith('H'):
               lastWasFacetLine = False
               if doFirstTitle:
                   bigName = t[2:].rstrip()
                   doFirstTitle = False
               else:
                   littleTitleLines.append(t[2:].rstrip())
                   
           
           elif t.startswith('a'):
               # Always follows 'g' line in spec
               facetList.append(parseFacetLine(t, 
                                               wheelIndex = wheelIndex))
               lastWasFacetLine = True
               
           elif t.startswith('F'):
               commentLines.append(t[2:].rstrip())
           
           else: 
               if lastWasFacetLine:
                   parseFacetLine(t, 
                                  wheelIndex = wheelIndex,
                                  appendTo = facetList[-1])
                   lastWasFacetLine = True
               else:
                   continue
                

                
           
       gemDict = {'gemCad_version' : gemCADver,
                  'wheelIndex' : wheelIndex,
                  'foldsymmetry' : foldSymmetry,
                  'mirrorPlane' : hasMirrorPlane,
                  'refractiveIndex' : refIndex,
                  'boldTitle' : bigName,
                  'littleTitle' : littleTitleLines,
                  'facetList' : facetList,
                  'comments' : commentLines}   
       
       #gemDict = correctDepthForRI(gemDict)
       
       return gemDict
   

def correctDepthForRI(gemDict):
    
    for i, x in enumerate(gemDict['facetList']):
        
        gemDict['facetList'][i]['depth'] =   gemDict['refractiveIndex'] * x['depth']**5
        
    return gemDict


def loadGemCADFile(pth):
    
    with open(pth, 'r') as fID:
        txt = fID.readlines()
        gemDict = parseAllLines(txt)
        
    return gemDict


if __name__ == "__main__":
    basePath = pathlib.Path(r'C:\Users\rusty\Documents\CAD\faceting\diagrams')
    fName = 'pc01082.asc'
    
    
    gemDict = loadGemCADFile(basePath / pathlib.Path(fName))
    
    
    print(gemDict)


















