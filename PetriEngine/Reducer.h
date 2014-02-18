/* 
 * File:   Reducer.h
 * Author: srba
 *
 * Created on 15 February 2014, 10:50
 */

#ifndef REDUCER_H
#define	REDUCER_H

#include "PetriNet.h"
#include "PQL/Contexts.h"

namespace PetriEngine{


/** Builder for building engine representations of PetriNets */
class Reducer

{
public:
	Reducer();
        void Print(PetriNet* net, MarkVal* m0);
        
private:

};


class CustomAnalysisContext: public PQL::AnalysisContext {
  
public:
    CustomAnalysisContext(const PetriNet& net):PQL::AnalysisContext(net) {}
	  
    ResolutionResult resolve(std::string identifier) const{
		ResolutionResult result;
       		result.offset = -1;
		result.success = false;
		for(size_t i = 0; i < _places.size(); i++){
			if(_places[i] == identifier){
				result.offset = i; 
                              	result.isPlace = true;
				result.success = true;
                                fprintf(stderr,"In query: %i\n\n",i);
                                return result;
			}
		}
		for(size_t i = 0; i < _variables.size(); i++){
			if(_variables[i] == identifier){
				result.offset = i;
				result.isPlace = false;
				result.success = true;
				return result;
			}
		}
		return result;
	}
    
};


}


#endif	/* REDUCER_H */

