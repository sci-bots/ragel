/*
 *  Copyright 2001-2014 Adrian Thurston <thurston@complang.org>
 */

#include "ragel.h"
#include "switchgotoloop.h"
#include "redfsm.h"
#include "bstmap.h"
#include "gendata.h"

void SwitchGotoLoop::tableDataPass()
{
	taActions();
	taToStateActions();
	taFromStateActions();
	taEofActions();
}

void SwitchGotoLoop::genAnalysis()
{
	/* For directly executable machines there is no required state
	 * ordering. Choose a depth-first ordering to increase the
	 * potential for fall-throughs. */
	redFsm->depthFirstOrdering();

	/* Choose default transitions and the single transition. */
	redFsm->chooseDefaultSpan();
		
	/* Choose single. */
	redFsm->chooseSingle();

	/* If any errors have occured in the input file then don't write anything. */
	if ( gblErrorCount > 0 )
		return;

	/* Anlayze Machine will find the final action reference counts, among other
	 * things. We will use these in reporting the usage of fsm directives in
	 * action code. */
	analyzeMachine();

	/* Run the analysis pass over the table data. */
	setTableState( TableArray::AnalyzePass );
	tableDataPass();

	/* Switch the tables over to the code gen mode. */
	setTableState( TableArray::GeneratePass );
}

void SwitchGotoLoop::writeData()
{
	if ( redFsm->anyActions() )
		taActions();

	if ( redFsm->anyToStateActions() )
		taToStateActions();

	if ( redFsm->anyFromStateActions() )
		taFromStateActions();

	if ( redFsm->anyEofActions() )
		taEofActions();

	STATE_IDS();
}

std::ostream &SwitchGotoLoop::ACTION_SWITCH()
{
	/* Walk the list of functions, printing the cases. */
	for ( GenActionList::Iter act = actionList; act.lte(); act++ ) {
		/* Write out referenced actions. */
		if ( act->numTransRefs > 0 ) {
			/* Write the case label, the action and the case break. */
			out << "\t case " << act->actionId << ":\n";
			ACTION( out, act, IlOpts( 0, false, false ) );
			out << "\n\tbreak;\n";
		}
	}

	return out;
}

std::ostream &SwitchGotoLoop::EOF_ACTION_SWITCH()
{
	/* Walk the list of functions, printing the cases. */
	for ( GenActionList::Iter act = actionList; act.lte(); act++ ) {
		/* Write out referenced actions. */
		if ( act->numEofRefs > 0 ) {
			/* Write the case label, the action and the case break. */
			out << "\t case " << act->actionId << ":\n";
			ACTION( out, act, IlOpts( 0, true, false ) );
			out << "\n\tbreak;\n";
		}
	}

	return out;
}

std::ostream &SwitchGotoLoop::FROM_STATE_ACTION_SWITCH()
{
	/* Walk the list of functions, printing the cases. */
	for ( GenActionList::Iter act = actionList; act.lte(); act++ ) {
		/* Write out referenced actions. */
		if ( act->numFromStateRefs > 0 ) {
			/* Write the case label, the action and the case break. */
			out << "\t case " << act->actionId << ":\n";
			ACTION( out, act, IlOpts( 0, false, false ) );
			out << "\n\tbreak;\n";
		}
	}

	return out;
}

std::ostream &SwitchGotoLoop::TO_STATE_ACTION_SWITCH()
{
	/* Walk the list of functions, printing the cases. */
	for ( GenActionList::Iter act = actionList; act.lte(); act++ ) {
		/* Write out referenced actions. */
		if ( act->numToStateRefs > 0 ) {
			/* Write the case label, the action and the case break. */
			out << "\t case " << act->actionId << ":\n";
			ACTION( out, act, IlOpts( 0, false, false ) );
			out << "\n\tbreak;\n";
		}
	}

	return out;
}


std::ostream &SwitchGotoLoop::EXEC_FUNCS()
{
	/* Make labels that set acts and jump to execFuncs. Loop func indicies. */
	for ( GenActionTableMap::Iter redAct = redFsm->actionMap; redAct.lte(); redAct++ ) {
		if ( redAct->numTransRefs > 0 ) {
			out << "	f" << redAct->actListId << ": " <<
				"_acts = offset( " << ARR_REF( actions ) << ", " << itoa( redAct->location+1 ) << ");"
				" goto execFuncs;\n";
		}
	}

	out <<
		"\n"
		"execFuncs:\n";

	if ( redFsm->anyRegNbreak() )
		out << "	_nbreak = 0;\n";

	out <<
		"	_nacts = (uint)deref( " << ARR_REF( actions ) << ", _acts );\n"
		"	_acts += 1;\n"
		"	while ( _nacts > 0 ) {\n"
		"		switch ( deref( " << ARR_REF( actions ) << ", _acts ) ) {\n";
		ACTION_SWITCH() << 
		"		}\n"
		"		_acts += 1;\n"
		"		_nacts -= 1;\n"
		"	}\n"
		"\n";

	if ( redFsm->anyRegNbreak() ) {
		out <<
			"	if ( _nbreak == 1 )\n"
			"		goto _out;\n";
		outLabelUsed = true;
	}

	out <<
		"	goto _again;\n";
	return out;
}


void SwitchGotoLoop::writeExec()
{
	testEofUsed = false;
	outLabelUsed = false;

	out << "	{\n";

	if ( redFsm->anyRegCurStateRef() )
		out << "	int _ps = 0;\n";

	if ( redFsm->anyToStateActions() || redFsm->anyRegActions() 
			|| redFsm->anyFromStateActions() )
	{
		out << 
			"	index " << ARR_TYPE( actions ) << " _acts;\n"
			"	uint _nacts;\n";
	}

	out << "\n";

	if ( redFsm->anyRegNbreak() ) {
		out << "	int _nbreak;\n";
	}

	if ( !noEnd ) {
		testEofUsed = true;
		out << 
			"	if ( " << P() << " == " << PE() << " )\n"
			"		goto _test_eof;\n";
	}

	if ( redFsm->errState != 0 ) {
		outLabelUsed = true;
		out << 
			"	if ( " << vCS() << " == " << redFsm->errState->id << " )\n"
			"		goto _out;\n";
	}

	out << "_resume:\n";

	if ( redFsm->anyFromStateActions() ) {
		out <<
			"	_acts = offset( " << ARR_REF( actions ) << ", " <<
					ARR_REF( fromStateActions ) << "[" << vCS() << "] );\n"
			"	_nacts = (uint) deref( " << ARR_REF( actions ) << ", _acts ); _acts += 1;\n"
			"	while ( _nacts > 0 ) {\n"
			"		switch ( deref( " << ARR_REF( actions ) << ", _acts ) ) {\n";
			FROM_STATE_ACTION_SWITCH() <<
			"		}\n"
			"		_acts += 1;\n"
			"		_nacts -= 1;\n"
			"	}\n"
			"\n";
	}

	out <<
		"	switch ( " << vCS() << " ) {\n";
		STATE_GOTOS() <<
		"	}\n"
		"\n";
		TRANSITIONS() <<
		"\n";

	if ( redFsm->anyRegActions() )
		EXEC_FUNCS() << "\n";

	out << "_again:\n";

	if ( redFsm->anyToStateActions() ) {
		out <<
			"	_acts = offset( " << ARR_REF( actions ) << ", " <<
					ARR_REF( toStateActions ) << "[" << vCS() << "] );\n"
			"	_nacts = (uint) deref( " << ARR_REF( actions ) << ", _acts ); _acts += 1;\n"
			"	while ( _nacts > 0 ) {\n"
			"		switch ( deref( " << ARR_REF( actions ) << ", _acts ) ) {\n";
			TO_STATE_ACTION_SWITCH() <<
			"		}\n"
			"		_acts += 1;\n"
			"		_nacts -= 1;\n"
			"	}\n"
			"\n";
	}

	if ( redFsm->errState != 0 ) {
		outLabelUsed = true;
		out << 
			"	if ( " << vCS() << " == " << redFsm->errState->id << " )\n"
			"		goto _out;\n";
	}

	if ( !noEnd ) {
		out << 
			"	" << P() << " += 1;\n"
			"	if ( " << P() << " != " << PE() << " )\n"
			"		goto _resume;\n";
	}
	else {
		out << 
			"	" << P() << " += 1;\n"
			"	goto _resume;\n";
	}

	if ( testEofUsed )
		out << "	_test_eof: {}\n";

	if ( redFsm->anyEofTrans() || redFsm->anyEofActions() ) {
		out << 
			"	if ( " << P() << " == " << vEOF() << " )\n"
			"	{\n";

		if ( redFsm->anyEofTrans() ) {
			out <<
				"	switch ( " << vCS() << " ) {\n";

			for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
				if ( st->eofTrans != 0 ) {
					RedCondAp *cond = st->eofTrans->outConds[0].value;
					out << "	case " << st->id << ": goto ctr" << cond->id << ";\n";
				}
			}

			out <<
				"	}\n";
		}

		if ( redFsm->anyEofActions() ) {
			out <<
				"	index " << ARR_TYPE( actions ) << " __acts;\n"
				"	uint __nacts;\n"
				"	__acts = offset( " << ARR_REF( actions ) << ", " << 
						ARR_REF( eofActions ) << "[" << vCS() << "] );\n"
				"	__nacts = (uint) deref( " << ARR_REF( actions ) << ", __acts ); __acts += 1;\n"
				"	while ( __nacts > 0 ) {\n"
				"		switch ( deref( " << ARR_REF( actions ) << ", __acts ) ) {\n";
				EOF_ACTION_SWITCH() <<
				"		}\n"
				"		__acts += 1;\n"
				"		__nacts -= 1;\n"
				"	}\n";
		}

		out <<
			"	}\n"
			"\n";
	}

	if ( outLabelUsed )
		out << "	_out: {}\n";

	out << "	}\n";
}
