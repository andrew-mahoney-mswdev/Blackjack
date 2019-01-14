#include <iostream>
#include <conio.h>
#include <sstream>
#include <thread>
#include <mutex>
#include <cstdarg> //Enbles passing of unspecified number of variables
#include <unistd.h> //Sleep
#include "filemngr.h"

using namespace std;

//Predeclaration of classes
class dhand;
class therootnode;
class node;

//Declaration of global variables is supposed to be bad practice but saves time by not passing the variables around everywhere.
dhand* dealerhand[54433]; //Every possible dealer hand is included in this array.
therootnode* rootnode; //The rootnode of the player hand game tree.
long double divide[129][417]; //It is quicker to precalculate division values and recall these as needed.  Array value records result of [x]/[y].
unsigned short int upcard; //The dealer's upcard.  This is global so it can be easily accessed by the threads.

thread* calculate[10]; //Ten threads remain dorment to perform calculations whenever they are queued.
bool active[10]; //Variables denote whether each thread is currently calculating.
node* queue[500000]; //The addess of node elements that need an roi calculated are stored here so they can be processed by threads.  In testing we approached 499000 when examining tree of card 1.
unsigned int queuelifo; //Records the number of node addresses in the queue.
mutex queueL, queueN, queueM; //These mutexes control priority access to the queue array.

filemngr* roifile;
float totalreturn;
unsigned long int betcount;

string str(float number) {stringstream ssintstr; ssintstr << number; return ssintstr.str();} //Converts a variety of numerical variables to strings for display purposes.
string str(long double number) {stringstream ssintstr; ssintstr << number; return ssintstr.str();}
string str(unsigned int number) {stringstream ssintstr; ssintstr << number; return ssintstr.str();}
string str(unsigned long int number) {stringstream ssintstr; ssintstr << number; return ssintstr.str();}
string str(unsigned short int number) {stringstream ssintstr; ssintstr << number; return ssintstr.str();}

void definedivisionarray(){ //Division values are precalculated.
	unsigned short int dividend, divisor;
	
	divisor = 0;
	for (dividend = 0; dividend < 129; dividend++) {divide[dividend][divisor] = 0;} //The outcome of division by 0 is recorded as 0.

	for (divisor = 1; divisor < 417; divisor++){ 
		for (dividend = 0; dividend < 129; dividend++){
			divide[dividend][divisor] = (long double)dividend / (long double)divisor;
		}
	}
}

//Represents the current state of a dealer's shoe
class shoe{
private:
    unsigned short int cardsleft; //The number of cards in the shoe.
    unsigned short int cardcount[11]; //The number of cards for each cardvalue.  Array elements 1-10 are used to represent their corresponding values. Element 1 represents and ace.
	//Element 0 is not used.  This method saves time.
public:
    shoe();
    shoe(shoe* source); //Creates an object that's a copy of an existing shoe.
    void deal(unsigned short int cardvalue); //Removes a card from the shoe.
	void insert(unsigned short int cardvalue); //Adds a card to the shoe.
    unsigned short int getcardsleft() {return cardsleft;} //Used for display purposes.
    unsigned short int* getcardsleftptr() {return &cardsleft;} //Providing access to these pointers enables node::prange function to perform faster.
    unsigned short int* getcardcountptr() {return cardcount;}
    void display(); //Displays cardcounts.
};

shoe::shoe(){
    unsigned short int index;
    
    cardsleft = 416; //Initial values assume 8 decks in the shoe.
    cardcount[0] = 0;
    for (index = 1; index < 10; index++) {cardcount[index] = 32;}
    cardcount[10] = 128; //There four different cards of value 10 (10, J, Q, K)
}

shoe::shoe(shoe* master){
    unsigned short int index;
    
    cardsleft = (*master).cardsleft; //Load values form the shoe being copied.
    for (index = 0; index < 11; index++) {cardcount[index] = (*master).cardcount[index];}
}

void shoe::deal(unsigned short int index){ //Note, the total of each card count should always sum to cardsleft.
    cardcount[index]--;
    cardsleft--;
}

void shoe::insert(unsigned short int index){
    cardcount[index]++;
    cardsleft++;
}

void shoe::display(){
    unsigned short int index;
    
    for (index = 1; index < 11; index++) {
        cout << index << ": " << cardcount[index] << endl;
    }
}

//Represents a possible dealer hand.  There are 54,433 possible hands when dealer stands on 17.
class dhand{
private:
	unsigned short int card[12]; //The cards in the hand in the order in which they're drawn.  There's a maximum of 12 cards in a hand.
	unsigned short int size; //How many cards ther are in the hand.
	unsigned short int firstunique; //The first element in card[] that is different from the card[] array of the previously created dhand object.
public:
	dhand(dhand* previous, int cardvalue...); //One possible dealer hand is passed to the constructor, with an indeterminat number of arguments.
	unsigned short int* getcardarray() {return card;} //Providing access to this pointer enables the node::prange function to perform faster.
	unsigned short int getsize() {return size;}
	unsigned short int getfirstunique() {return firstunique;}
};

dhand::dhand(dhand* previous, int cardvalue...){
	va_list cardlist; //The cards dealt to the dealer, in order.
	unsigned short int index;
	unsigned short int score; //The score of the dealer hand.  I don't know yet if this data is required.
	unsigned short int softhand = 0; //The equivalent soft score.
	bool ace; //Whether there is an ace in the hand.
		
//The following determines when to end the cardlist varliable, based upon the score of the hand.
//If the score variable is not going to be required, this can be rewritten so that the cardlist passes the size variable and ends accordingly.
//This will be much simpler code!  However, if score is required there's no poing adding another variable to cardlist.
	va_start(cardlist, cardvalue); //Get the first variable
	card[0] = cardvalue;
	score = card[0];
	if (card[0] != 1) {ace = false;} else {ace = true;} //The presence of an ace determines whether the soft score is available.
	index = 1;
	do {
		card[index] = va_arg(cardlist, int); //Get the next variable
		score += card[index]; 
		softhand = score + 10;
		if (card[index] == 1) {ace = true;} //Ace can always be set to true, but never back to false.
		if (ace == true && softhand >= 17 && softhand <= 21) {score = softhand;} //If there's an ace and a valid soft score, we use the soft score.
		index++;
	} while (score < 17); //Dealer must stand on 17!
	va_end(cardlist);

	size = index;
	for (; index < 12; index++) {card[index] = 0;} //Remaining varialbes in array are set to 0 for good measure.

	index = 0;
	if (previous != NULL) {
		while (previous->card[index] == card[index]) {index++;} //Find the first card that differs from the previous dhand.
		if (index == 0) {index = 1;} //This is necessary so that prange does not redeal the card[0] (ie. the upcard) back into the shoe.
		previous->firstunique = index; //Set the firstunique variable of the previous dhand.
	}
	firstunique = 1; //A default value that will only be saved by the last object.
}

class therootnode { //The required properties of the first node of the player hand game tree and the parent of the node class.
protected:
	unsigned short int size; //The number of cards in the tree.  Used to detect possible blackjack.
	unsigned short int total; //The total hard score value of the hand.
	bool ace; //Whether the hand includes an ace.
	node* branch[10]; //The address of all branches from this node. IE. the address of the node if an ace is dealt, or a two, etc. Note that in this array, element 0 represents the ace branch, 1 represents the 2 branch, etc.
	void extend(); //Function to create ten new branches of the game tree, extending from this node.
public:
	therootnode();
	void drawgametree() {extend();} //This cannot be part of the therootnode constructor, which is called everytime the node constructor is called.  This function should only ever be called once.
	node* getbranch(unsigned short int index) {return branch[index-1];} //Returns the address of a branch.
friend class node; //This is necessary so that the constructor for node can access the protected features of therootnode.
};

class node : public therootnode {
private:
	unsigned short int card; //The card value of the path leading to this node.
	unsigned short int score; //The real score (hard or soft) of the node.
	long double standroi; //The return on investment if the player stands at this node.
	long double hitroi; //The weighted average return on investment if the player hits at this node.
	long double doubleroi; //The return on investment if only one hit is taken.
	long double* roiptr; //Points to the highest roi. This is the return on investment of the node.
	shoe* deck; //The state of the shoe at this node.
	void end(); //Function called during drawing of game tree when there are no new nodes that emerge from this one.
	void throwshoe(shoe* deck); //Called after the public function load(). Creates a deck object for every child node.
	long double prange(unsigned short int start, unsigned short int end); //Calculates the total probability that a given set of dealer hands will occur.
//The roifunc variable can point to any one of these functions, corresponding to the score of the hand.
	void lowhand();
	void seventeen();
	void eighteen();
	void nineteen();
	void twenty();
	void twentyone();
	void blackjack();
public:
	node(therootnode* parent, unsigned short int newcard);
	void (node::*roifunc)(); //Variable pointing to a function which calculates the standroi.
	void load(shoe* master); //Copies maindeck to the node, and calls the throwshoe function.
	void calcstandroi(); //Inserts the address of all nodes that need standroi to be calculated into the node queue.
	void calchitroi(); //Sums all standroi values by the probability of each card being drawn, calculating the average return for hitting.
	void calcdoubleroi(); //Looks at the standroi only one level down.
	void resethitroi() {hitroi = 0;} //This function is only used after splitting aces. Only one hit is allowed, so hitroi of pnode must be set to 0 to force a stand at this node.
	unsigned short int getcard() {return card;} //Only used to determine if we are splitting aces.
	long double getstandroi() {return standroi;}
	long double gethitroi() {return hitroi;}
	long double getdoubleroi() {return doubleroi;}
	unsigned short int getscore() {return score;}
friend void calcsplitaceroi(); //This is a friend so that the twnetyone function can be accessed manually.
};

therootnode::therootnode(){ //These values are set so that the node constructor can reference them.
	size = 0;
	total = 0;
	ace = false;
}

node::node(therootnode* parent, unsigned short int newcard) {
	unsigned short int softhand;

	card = newcard;
	size = parent->size + 1;
	total = parent->total + card;
	if (newcard != 1) {ace = parent->ace;} else {ace = true;} //Ace can change to true but neer back to false.
	softhand = total + 10;
	if (ace == true && softhand <= 21) {score = softhand;} else {score = total;} //Determine whether to use hard or soft score.
	if (score < 21) {extend();} else {end();} //Hitting is possible for every value below 21.
	deck = NULL;

//Determine the roifunc:
    if (score > 21) { //ROI is always 0 if hand busts.
	    roifunc = 0;
		standroi = 0;
	}
    else if (score <= 16) {roifunc = &node::lowhand;}
    else {
    	switch(score){
		case 17: roifunc = &node::seventeen; break;
		case 18: roifunc = &node::eighteen; break;
		case 19: roifunc = &node::nineteen; break;
		case 20: roifunc = &node::twenty; break;
		case 21: 
			if (size != 2) {roifunc = &node::twentyone;} //Determine whether 21 is a blackjack.
			else {roifunc = &node::blackjack;}
		break;
		}
    }
}

void therootnode::extend(){
	unsigned short int index;
	
	for (index = 0; index < 10; index++) { //Create 10 new nodes extending from this one.
		branch[index] = new node(this, index + 1);
	}
}

void node::end(){
	unsigned short int index;
	
	for (index = 0; index < 10; index++) { //NULL value denotes there are no more nodes.
		branch[index] = NULL;
	}	
}

void node::load(shoe* master){
	unsigned short int index;
	
	if (deck != NULL) {delete deck;}
	deck = new shoe(master); //This node copies the deck representing the current state of the shoe.
	if (branch[0] != NULL) {
		for (index = 0; index < 10; index++){
			branch[index]->throwshoe(deck);
		}
	}
}

void node::throwshoe(shoe* parent){
	unsigned short int index;
	
	if (deck != NULL) {delete deck;}
	deck = new shoe(parent); //Copy the deck of the parent and deal the card which must be dealt to reach this node.
	deck->deal(card);
	if (branch[0] != NULL) { //If we are not at the end of a path, continue to copy decks to other nodes.
		for (index = 0; index < 10; index++){
			branch[index]->throwshoe(deck);
		}
	}	
}

void node::calcstandroi(){
	unsigned short int index;

	//standroi doesn't need to be set to 0 as its value is assigned in prange
	if (roifunc != 0) { //No need to calculate anything if hand is a bust.
		queueN.lock(); //This mutex sequence allows this function to have priority when accessing the queue.
		queueM.lock();
		queueN.unlock();
		queue[queuelifo] = this; //The value of the current node is loaded, and queuelifo incremented.
		queuelifo++;
		queueM.unlock();
	}
	
	if (branch[0] != NULL) { //All other nodes that require calculation are loaded into the queue.
		for (index = 0; index < 10; index++) {
			branch[index]->calcstandroi();
		}
	}
}

void node::calchitroi(){
//This function essentially traces down the game tree until the end of a path in which an ace will only give a positive return.
//It then traces back up the tree and the positive return of a hit will increase further up the tree.
	unsigned short int index;	
	unsigned short int* cardsleft = deck->getcardsleftptr();
	unsigned short int* cardcount = deck->getcardcountptr();

	hitroi = 0; //Variable must be initialised so that it doesn't default to nan
	if (branch[0] != NULL) { //Most nodes do actually end.  A hit cannot be taken from these nodes and for most of them a stand returns 0 due to a bust.
		for (index = 0; index < 10; index++){
			branch[index]->calchitroi(); //Hit roi of each node must be calculated before it can be summed.
			hitroi += *branch[index]->roiptr * divide[cardcount[index+1]][*cardsleft]; //Sums the best roi of each node multiplied by the probability of the card being drawn.
		}
	}
	
	if (standroi >= hitroi) {roiptr = &standroi;} //roiptr denotes the best roi of the node, which is the overall roi on optimal strategy.
	else {roiptr = &hitroi;}
}

void node::calcdoubleroi(){
	unsigned short int index;	
	unsigned short int* cardsleft = deck->getcardsleftptr();
	unsigned short int* cardcount = deck->getcardcountptr();

	doubleroi = 0;
	if (branch[0] != NULL) {
		for (index = 0; index < 10; index++){ //Double forces player to stand after one hit, so we only examine standroi down to one level.
			doubleroi += branch[index]->standroi * divide[cardcount[index+1]][*cardsleft];
		}
	}
}

long double node::prange(unsigned short int start, unsigned short int end){ //The first and last values of dealerhand[] that will be examined.
//A lot of code that should be external to this is internal. This saves time on procuedure calls.
	unsigned short int index; //The index value of dealerhand[]
	unsigned short int dnodeindex = 1; //The index of the dealer hand "node".  IE. the current element of dhand.card[]
	long double probability = 0; //Total probability returned by the function.
	long double dhandp = 0; //The probability that the current hand will occur.
	long double pofindex[12]; //A record of the p value of at every dealer hand node. Note, the first two values of this array don't need to be used.
	unsigned short int size, firstunique;
	unsigned short int* card; //Points to the card array of dealerhand. Allows manipulation of array without calling external funcitons.
	unsigned short int* cardsleft = deck->getcardsleftptr(); //Points to the cardsleft value in the deck.  Allows deal() and insert() functions to be insourced.
	unsigned short int* cardcount = deck->getcardcountptr(); //Points to the cardcount array in the deck.

	for (index = start; index <= end; index++){ //Note that the last value (end) is processed.
		card = dealerhand[index]->getcardarray(); //Get the card array, size and firstunique for each instance of dealerhand[].
		size = dealerhand[index]->getsize();
		firstunique = dealerhand[index]->getfirstunique();

		if (dnodeindex != 1) {dhandp = pofindex[dnodeindex];} //Get the p value of the current index.
		else {
			dhandp = divide[cardcount[card[1]]][*cardsleft]; //Get the p value of the first index.
			cardcount[card[1]]--; //deck.deal(card);
			(*cardsleft)--;
			dnodeindex++;
		}
	
		for (; dnodeindex < size; dnodeindex++){
			pofindex[dnodeindex] = dhandp; //On re-execution of loop, save the dhandp value so that it can be recalled for firstunique.
			dhandp *= divide[cardcount[card[dnodeindex]]][*cardsleft]; //Multiply the current probability of the hand, by the probability of drawing the next card.
			cardcount[card[dnodeindex]]--; //deck.deal(card);
			(*cardsleft)--;
		}

		while (dnodeindex > firstunique) { //Revert the deck back to the state at firstunique (quicker than copying). Note that if the range overlaps an upcard, this will insert card[0].
			dnodeindex--;
			cardcount[card[dnodeindex]]++; //deck.insert(card);
			(*cardsleft)++;
		}

		probability += dhandp; //Add to the total probability of the range of hands.
	}

	while (dnodeindex > 1) { //Reset the shoe back to its state at start of loop. This is necessary when the range doesn't end with a firstunique value of 1 (ie. when the dealer has an ace but no blackjack).
		dnodeindex--;
		cardcount[card[dnodeindex]]++; //deck.insert(card);
		(*cardsleft)++;
	}

	return probability;
} //Note: this function leaves the shoe all messed up.  Copy shoe if you need to preserve it.

long double getinsuranceroi (shoe* deck) { //Check the return for buying insurance. This is checked as a separate bet from the main bet.
	unsigned short int* cardsleft = deck->getcardsleftptr();
	unsigned short int* cardcount = deck->getcardcountptr();

	return divide[cardcount[10]][*cardsleft] * 3; //Insurance should be bought if the return is greater than 1.
}

void calculate_thread(bool* active){ //Threads continually run this function to automatically calculate the prange of node objects in the queue.
	node* mynode;
	*active = false;
	
	while (true) { //This mutex sequence allows the thread to have 2nd priority access to the queue.
		queueL.lock(); //When there is nothing to process queueL will be locked, preventing CPU resouces from being consumed.
		queueN.lock();
		queueM.lock();
		queueN.unlock();
		if (queue[0] != NULL) {
			*active = true; //We note that the thread is processing.
			queuelifo--; //Get the top node address in the queue and reduce queuelifo.
			mynode = queue[queuelifo];
			queue[queuelifo] = NULL;
			queueM.unlock();
			queueL.unlock();

			(mynode->*(mynode->roifunc))(); //Execute roifunc only if an address has been sourced and after the mutex is freed.
			*active = false; //The thread has finished processing.
		} else {
			queueM.unlock(); //If there is nothing in the queue, free the mutex.
			queueL.unlock();
		}
	}
}

void wait4inactivity(){ //Function uses a global array of 10 booleens, denoting whether each thread is still processing.
	unsigned short int index;
	
	while (queuelifo > 0) {usleep(1);} //Processing cannot complete as long as there are nodes left in the queue.
	for (index = 0; index < 10; index++){
		while (active[index] == true) {usleep(1);} //We wait for each thread to finish.
	}
}

void calcroi(shoe* deck, node* pnode){ //Calculates all roi values in each node in the possibility space of the hand.
	pnode->load(deck); //Each node is allocated a copy of the deck that will exist at that node.
	queueL.unlock(); //Unlocking the mutex makes the threads active, and uses 100% of CPU power.
	pnode->calcstandroi(); //Loads the address of all nodes in the possibility space into the quese for the threads to calculate.
	wait4inactivity(); //Waits for queue to empty and threads to cease calculation (uses the global active array for this).
	queueL.lock(); //Halts all threads, reducing load on CPU.
	pnode->calchitroi(); //hitroi and doubleroi are dependent on the value of standroi.
	pnode->calcdoubleroi();		
}

void calcsplitaceroi(); //Predeclaration of more complex function
void calcroi(shoe* deck, node* pnode, node* splitnode){ //Calculates all roi values when splitting is an option.
	node* focusnode; //Records parent of all node addresses that must be calculated.

	splitnode->load(deck); //Copies of the deck are permeated through all branches of the splitnode.
	pnode->load(deck); //Copies of the SAME deck are permeated from the pnode (as not splitting is a viable option). This may require recalculation when no splitting.
	if (splitnode->getcard() != 1) {
		focusnode = splitnode; //When not splitting aces we focus on the splitnode.
	}
	else {
		calcsplitaceroi(); //This func calculates the special case of splitting aces.
		focusnode = pnode; //We then only need to focus on the pnode.
	}
	queueL.unlock();
	focusnode->calcstandroi(); //Calc standroi values of all branches of the focusnode.
	wait4inactivity();
	queueL.lock();
	focusnode->calchitroi();
	pnode->calcdoubleroi();	
}

void calcsplitaceroi(){ //Used to calculate the roi for splitting aces, which only allows one hit and no blackjack.
	unsigned short int index;
	node* splitnode = rootnode->getbranch(1); //This is the only node for which this function will ever be run.
	unsigned short int* cardsleft = (splitnode->deck)->getcardsleftptr();
	unsigned short int* cardcount = (splitnode->deck)->getcardcountptr();

	queueL.unlock(); //queueL is controlled within the function as it must be relocked before hitroi is calculated.
	for (index = 1; index < 10; index++) { //The first 9 branches are calculated as normal.
		queueN.lock();
		queueM.lock();
		queueN.unlock();
		queue[queuelifo] = splitnode->getbranch(index);
		queuelifo++;
		queueM.unlock();
	}
	(splitnode->getbranch(10))->twentyone(); //The tenth branch (ace + ten) is calculated as a twentyone. This is done from the main thread as it is a shorter function and will end before other threads.

	wait4inactivity();
	queueL.lock();

	splitnode->hitroi = 0; //hitroi is calculated in the same way as double (only one hit being allowed)
	for (index = 0; index < 10; index++){
		splitnode->hitroi += (splitnode->branch[index])->standroi * divide[cardcount[index+1]][*cardsleft];
	}
}

enum facemode {deal, hit, stand, x2, split, dplay, payout}; //All possible output modes
enum vero {notchecked, notinsured, insured}; //Insurance states

class interface{ //The interface class controls the outcome to the console, including calculating the optimal strategy based on roi values.
	private:
		facemode mode;
		shoe* maindeck; //The shoe to be displayed
		node** pnodeptr; //The node of the player's current hand.
		node** pnode2ptr; //The node of the player's second hand if the player has been split.
		node** splitnodeptr; //The node from which a split may occur (ie. the node of a single card only).
		long double hitroi, standroi, doubleroi, splitroi; //The roi values of the various options.
		long double avgposthit, avgpoststand, avgpostdouble, avgpostsplit; //The "average" average rois after each strategy, used to calculate the best strategy.
		long double avgroi; //The average of all play outcomes over the lifetime of the program (used to calculate split).
		bool splitoption; //Whether the split option should be displayed on screen (if available, it should only be displayed once).
		bool splitaces, doublebet; //Whether the player has split aces or doubled. Used for payout calculation
		bool dace; //These are used for calculating the dealer's score.
		unsigned short int dtotal;
		float calcroi(); //Determines the roi at the end of the hand.
		void header(string status); //Rollback analysis software designed by me!
		void display(); //Determines which display function to call.
		void delay(string status); //Displayed when no game is in progress (ie. cards are being burned).
		void play(); //Displayed when hand is being played.
	public:
		unsigned short int pcard1, pcard2, dcard; //The value of the cards in play that determine the possibility space of the hand.
		unsigned short int ptrick[2], dtrick; //The number of cards in each of the player's and the dealer's hands.
		unsigned short int numhands; //The number of hands being played. This will be 1 or 2.
		vero insurance; //They paid for the repairs to my house, I may as well name a variable after them!
		interface(shoe* themaindeck, node** thepnodeptr, node** thepnode2ptr, node** thesplitnodeptr);
		char update(); //Updates the display. Determines which prompt to display.
		char update(string prompt); //Displays the prompt that's been passed.
		char update(long double insuranceroi); //Determines and prompts whether or not to buy insurance.
		void calcmode(bool handnum); //Determines optimal strategy for play.
		void newdcard(unsigned short int cardvalue); //Adds a card to the dealer's hand.
		unsigned short int getdscore();
		facemode getmode() {return mode;}
};

interface::interface(shoe* themaindeck, node** thepnodeptr, node** thepnode2ptr, node** thesplitnodeptr) {
//Note that a new interface object is created with every turn, so that these values are reset.
	maindeck = themaindeck;
	pnodeptr = thepnodeptr;
	pnode2ptr = thepnode2ptr;
	splitnodeptr = thesplitnodeptr;
	mode = deal;
	pcard1 = 0;
	pcard2 = 0;
	dcard = 0;
	ptrick[0] = 0;
	ptrick[1] = 0;
	dtrick = 0;
	doublebet = false; //Can be set to true, but never back to false.
	splitaces = false; //Ditto
	splitoption = true; //Can be set to false, but never back to true.
	numhands = 1;
	insurance = notchecked;
	dtotal = 0;
	dace = false; //Again, only single change allowed.
	avgroi = (long double)totalreturn / betcount;
}

void interface::calcmode(bool handnum) { //The method for calculating optimal strategy is designed to maximise the average roi. The Kelly criterion can then be used to calculate optimal bets.
	node** handnodeptr; //The node of the hand for which we calculate the strategy.
		
	if (handnum == 0) {handnodeptr = pnodeptr;}
	else {handnodeptr = pnode2ptr;} //Passing 1 represents the second hand of a split.

	standroi = (*handnodeptr)->getstandroi();
	avgpoststand = (totalreturn + standroi) / (betcount + 1); //This is the "average" average roi that will result after a stand. The best strategy will maximise the average return.
	hitroi = (*handnodeptr)->gethitroi();
	avgposthit = (totalreturn + hitroi) / (betcount + 1);
	doubleroi = (*handnodeptr)->getdoubleroi();
	avgpostdouble = (totalreturn + (doubleroi * 2)) / (betcount + 2); //Double and split are counted as two separate bets.
	if (pcard1 != pcard2) {splitoption = false;} //Do not calculate split option is these don't match.
	if (splitoption == true) { //The splitroi is the hitroi of the splitnode.
		splitroi = (*splitnodeptr)->gethitroi();
		avgpostsplit = (totalreturn + (splitroi * 2)) / (betcount + 2);
	} else {splitroi = 0; avgpostsplit = 0;}

	if (mode == x2) {mode = stand; return;} //Must stand after a double.
	if (avgposthit > avgpoststand) { //The strategy resulting in the largest average return is chosen. If strategies are equal, the second option (which conserves cards in the shoe) is chosen.
		if (avgpostdouble > avgpostsplit) {
			if (avgpostdouble > avgposthit) {mode = x2;} else {mode = hit;}
		} else {
			if (avgpostsplit > avgposthit) {mode = split;} else {mode = hit;}
		}
	} else {
		if (avgpostdouble > avgpostsplit) {
			if (avgpostdouble > avgpoststand) {mode = x2;} else {mode = stand;}
		} else {
			if (avgpostsplit > avgpoststand) {mode = split;} else {mode = stand;}
		}
	}
	if (pcard1 == 1 && pcard2 == 1 && numhands == 2) {mode = stand;} //In the case of splitting aces, only 1 hit is allowed.

	if (mode == x2) {doublebet = true;}
	if (mode == split && pcard1 == 1) {splitaces = true;}
}

void interface::newdcard(unsigned short int cardvalue) {
	if (cardvalue == 1) {dace = true;}
	dtotal += cardvalue;
	if (getdscore() >= 17) {mode = payout;}
}

unsigned short int interface::getdscore() {
	unsigned short int dscore;
	
	if (dace == true && dtotal + 10 <= 21) {dscore = dtotal + 10;} else {dscore = dtotal;} //Differentiates between the soft and hard score.
	return dscore;
}

float interface::calcroi() {
	unsigned short int i, numbets = 1;
	float roi, gameroi = 0;
	unsigned short int dscore = getdscore();
	unsigned short int pscore[2];
	pscore[0] = (*pnodeptr)->getscore();
	if (numhands == 2) {pscore[1] = (*pnode2ptr)->getscore(); numbets = 2;}
	else if (doublebet == true) {pscore[1] = (*pnodeptr)->getscore(); numbets = 2;}
	
	for (i = 0; i < numbets; i++) {
		if (dscore == 21 && dtrick == 2 && pscore[i] == 21 && ptrick[i] == 2 && splitaces == false) {roi = 1;} else
		if (dscore == 21 && dtrick == 2) {roi = 0;} else
		if (pscore[i] == 21 && ptrick[i] == 2 && splitaces == false) {roi = 2.5;} else
		if (pscore[i] > 21) {roi = 0;} else
		if (dscore > 21) {roi = 2;} else
		if (dscore > pscore[i]) {roi = 0;} else
		if (dscore < pscore[i]) {roi = 2;}
		else roi = 1;
		
		if (insurance == insured && i == 0) { //Calculate insurance on the first bet
			roi -= 0.5;
			if (dscore == 21 && dtrick == 2) {roi += 1.5;}
		}
		
		gameroi += roi;
		totalreturn += roi;
		betcount++;
		roifile->writeline(roi);
	}
	
	return gameroi;
}

void interface::header(string status) {
	system("cls");
	cout << "Rollback analysis software for blackjack v1.0" << endl;
	cout << "                         by Dr Andrew Mahoney" << endl;
	cout << status << endl << endl;
	cout << "Cards: " << maindeck->getcardsleft() << endl;
	maindeck->display();
	cout << endl;
}

void interface::delay(string status) {
	header(status);
	cout << "Card 1: " << pcard1 << endl;
	cout << "Card 2: " << pcard2 << endl;
	cout << "Dealer: " << dcard << endl;
	cout << endl << endl << endl << endl << endl;
}

void interface::play() {
	string status;
	float gameroi;

	switch (mode) {
	case hit: status = "HIT"; break;
	case stand: status = "STAND"; mode = dplay; break; //After the stand, its the dealers turn. Note: Mode will be redetermined before dealer's turn when standing on first hand of two hands.
	case x2: status = "DOUBLE"; break;
	case split: status = "SPLIT"; break;
	case dplay: status = "DEALER"; break;
	case payout: 
		gameroi = calcroi();
		status = "Bet: $x, Payout: " + str(gameroi);
		break;
	}

	header(status);

	if (numhands == 1) {
		if (mode != split) {cout << "Hand:   " << (*pnodeptr)->getscore() << endl;}
		else {cout << "Hand:   " << (*pnodeptr)->getscore() << "/" << (*pnode2ptr)->getscore() << endl;}
	} else {
		cout << "Hand 1: " << (*pnodeptr)->getscore() << endl;
		cout << "Hand 2: " << (*pnode2ptr)->getscore() << endl;
	}
	cout << "Dealer: " << getdscore() << endl << endl;

	if (mode == payout) {
		cout << "Balance: $x" << endl;
		cout << "Bet:     $x (x.x)" << endl;
		cout << endl << endl;
		if (numhands == 1) {cout << endl;}
	} else if (mode != split || numhands == 1) { //This will only be false when getting the second card of the second hand.
		cout << "Hit:    " << hitroi << " (" << avgposthit << ")" << endl;
		cout << "Stand:  " << standroi << " (" << avgpoststand << ")" << endl;
		if (doubleroi > 0) {cout << "Double: " << doubleroi << " (" << avgpostdouble << ")" << endl;} else {cout << endl;}
		if (splitoption == true) {cout << "Split:  " << splitroi << " (" << avgpostsplit << ")" << endl; splitoption = false;} //Split option only displayed once.
		else if (numhands == 1) {cout << endl;}
		cout << endl;
	} else {cout << endl << endl << endl << endl;}
}

void interface::display() {
	if (mode != deal) {play();}
	else {delay("Return: " + str(avgroi) + " (" + str(betcount) + ")");} //Status displayed when cards are being burned.
}

char interface::update(string prompt) {
	display();
	cout << prompt << ">";
	return getch();
}

char interface::update() {
	string prompt;

	switch (mode) {
	case deal: prompt = "Deal"; break;
	case hit: prompt = "Hit"; break;
	case dplay: case stand: prompt = "Dealer"; break;
	case x2: prompt = "Double"; break;
	}
	
	display();
	cout << prompt << ">";
	return getch();
}

char interface::update(long double insuranceroi) {
	string status;

	if (insuranceroi <= 1) {
		status = "NO INSURANCE";
		insurance = notinsured;
	}
	else {
		status = "BUY INSURANCE";
		insurance = insured;
	}
	delay(status);
	cout << "Dealer BJ (y/n)>";
	return getch();
}

unsigned short int pickacard(char cmd) { //Verifies card value
	unsigned short int cardvalue;
	
	if (cmd >= '0' && cmd <= '9') {
		cardvalue = cmd - '0';
		if (cardvalue == 0) {cardvalue = 10;}
		return cardvalue;
	} else {return 0;} //Denotes that no card value was entered.
}

#include "dealerhandarray.h"

int main(int argc, char** argv) {
	unsigned int index;
	float loadroi;
	char cmd;
	unsigned short int cardvalue;
	shoe* maindeck;
	interface* screen;
	node *pnode[2], *splitnode, *initialnode;

	definedivisionarray();
	definedealerhandarray();
	rootnode = new therootnode;
	rootnode->drawgametree();

	queueL.lock();
	for (index = 0; index < 10; index++) {
		calculate[index] = new thread(calculate_thread, &active[index]);
		calculate[index]->detach();
	}

	if (findfile("bjroi.txt") == true) {
		roifile = new filemngr("bjroi.txt", in);
		for (totalreturn = 0, betcount = 0, loadroi = roifile->readfloat(); roifile->geteof() == false; totalreturn += loadroi, betcount++, loadroi = roifile->readfloat());
		roifile->changestate(app);
	} else {roifile = new filemngr("bjroi.txt", out);}

	maindeck = new shoe;
	screen = new interface(maindeck, &pnode[0], &pnode[1], &splitnode);

	do {
		cmd = screen->update("Deal"); //The default condition, removes cards from the deck.
		cardvalue = pickacard(cmd);
		if (cardvalue != 0) {maindeck->deal(cardvalue);}

		if (cmd == '+') { //Reinserts cards if an error was made.
			cmd = screen->update("Insert");
			cardvalue = pickacard(cmd);
			if (cardvalue != 0) {maindeck->insert(cardvalue);}
		}
		
		if (cmd == 'f' || cmd == 'F') { //Sets the first card of the player's hand.
			cmd = screen->update("Card 1");
			cardvalue = pickacard(cmd);
			if (cardvalue != 0) {
				maindeck->deal(cardvalue);
				screen->pcard1 = cardvalue;
			}
		}

		if (cmd == 'd' || cmd == 'D') { //Sets the dealer's card.
			cmd = screen->update("Dealer");
			cardvalue = pickacard(cmd);
			if (cardvalue != 0) {
				maindeck->deal(cardvalue);
				screen->dcard = cardvalue;
			}
		}

		if (cmd == 's' || cmd == 'S') { //Sets the second card of the player's hand.
			cmd = screen->update("Card 2");
			cardvalue = pickacard(cmd);
			if (cardvalue != 0) {
				maindeck->deal(cardvalue);
				screen->pcard2 = cardvalue;
			}
		}

		if (cmd == 'n' || cmd == 'N') { //Resets the shoe
			cmd = screen->update("Confirm new shoe (y)");
			if (cmd == 'y' || cmd == 'Y') {
				delete maindeck;
				maindeck = new shoe;
				delete screen;
				screen = new interface(maindeck, &pnode[0], &pnode[1], &splitnode);
			}
		}

		if (screen->dcard == 1 && screen->insurance == notchecked) { //Prompts for insurance strategy and checks for dealer blackjack.
			if (cmd == 'i' || cmd == 'I' || cmd == '\r') { //Checking insurance conditions before cmd allows us to force an insurance check if you press \r and not i
			//NOTE: DOES NOT PREVENT INSURANCE STRATEGY BEING PROMPTED MORE THAN ONCE, AND DOES NOT FORCE INSURANCE PROMPTING (MAY CAUSE PROBLEMS WHEN CALCULATING RETURN).
				if (screen->dcard == 1) {
					do {
						cmd = screen->update(getinsuranceroi(maindeck));
					} while (cmd != 'y' && cmd != 'Y' && cmd != 'n' && cmd != 'N');
					if (cmd == 'y' || cmd == 'Y') {
					//UPDATE THIS AND IF STATEMENT BELOW WITH DOCUMENTATION.
						maindeck->deal(10); //Add the 10 to the dealer's hand.
						screen->newdcard(10);
						screen->dtrick++;
						cmd = '\r'; //Force the turn to end. The dealer's hand will then be noted as a blackjack straight after.
					}
				}
			}
		}
		
		if (cmd == '\r') { //Plays out hand and displays optimal strategy.
			upcard = screen->dcard;
			screen->newdcard(upcard); //Initialises interface::dscore
			screen->dtrick++;
			pnode[0] = (rootnode->getbranch(screen->pcard1))->getbranch(screen->pcard2);
			screen->ptrick[0] = 2;
			initialnode = pnode[0];

			if (screen->pcard1 != screen->pcard2) { //THIS WILL BE MOVED TO ENABLE ADVANCE CALCULATION
				calcroi(maindeck, pnode[0]);
			} else {
				splitnode = rootnode->getbranch(screen->pcard1);
				calcroi(maindeck, pnode[0], splitnode);
			}

if (screen->getdscore() != 21) { //Do not play if dealer has a confirmed blackjack. Out of structure command makes code easier to follow.
			screen->calcmode(0);
			if (screen->getmode() == split) { //Gets the second cards for the two hands after a split.
				pnode[0] = splitnode; //These assignments enable the correct score to be displayed while the second cards are being dealt.
				pnode[1] = splitnode;
				for (index = 0; index < 2; index++) {
					do {
						cmd = screen->update((string)"Hand " + str(index + 1));
						cardvalue = pickacard(cmd);
					} while (cardvalue == 0);
					maindeck->deal(cardvalue);
					pnode[index] = splitnode->getbranch(cardvalue);
					screen->numhands = 2; //Setting this value here, enables the correct screen to display when getting the second card of the second hand.
					}
				screen->ptrick[1] = 2;
			}

			for (index = 0; index < screen->numhands; index++){
				if (index == 1) {screen->update("Stand (r)");} //Stand strategy is displayed at end of first hand which sets dplay mode, but this is recalculated.
				if (screen->getmode() != x2) {screen->calcmode(index);} //Repeat calculation on first iteration of loop confuses calcmode() if strategy is double.
				while (screen->getmode() != stand) { //Prompts with optimal strategy until hand is stand or bust
					do {
						cmd = screen->update();
						cardvalue = pickacard(cmd);
					} while (cardvalue == 0);
					maindeck->deal(cardvalue);
					pnode[index] = pnode[index]->getbranch(cardvalue);
					screen->ptrick[index]++;
					screen->calcmode(index);
				}
			}

			do { //The dealer's turn
				do {
					cmd = screen->update();
					cardvalue = pickacard(cmd);
				} while (cardvalue == 0);
				maindeck->deal(cardvalue);
				screen->newdcard(cardvalue);
				screen->dtrick++;
			} while (screen->getdscore() < 17);
} //End of out of structure if statement
			
			screen->update("Continue (r)");

			delete screen; //Renew all default interface variables at end of turn.
			screen = new interface(maindeck, &pnode[0], &pnode[1], &splitnode);
		}

	} while (cmd != '\e'); //Exits program
	
	return 0;
}

//These are all the permutations of the roifunc function variable of node.
//The roifunc that is used depends on the value of the player's hand.
//The function scans the dealer hands that begin with the relevant upcard, the probability of each hand is determined and multiplied by the return to the player.
//This calculates the total average return.

void node::lowhand() {
	switch (upcard){
	case 1: standroi = 2 * prange(0, 3776); standroi /= 1 - prange(8496, 8496); break;
	case 2: standroi = 2 * prange(8497, 16867); break;
	case 3: standroi = 2 * prange(27218, 32193); break;
	case 4: standroi = 2 * prange(38343, 41290); break;
	case 5: standroi = 2 * prange(44932, 46732); break;
	case 6: standroi = 2 * prange(48956, 49987); break;
	case 7: standroi = 2 * prange(51261, 51905); break;
	case 8: standroi = 2 * prange(52702, 53088); break;
	case 9: standroi = 2 * prange(53567, 53824); break;
	case 10: standroi = 2 * prange(54144, 54272); break;
	}
}

//Note: Can rewrite the case 1s so that they don't calculate 8496 twice.

void node::seventeen() {
	switch (upcard){
	case 1: standroi = prange(3777, 4720); standroi += 2 * (1 - (standroi + prange(4721, 8496))); standroi /= 1 - prange(8496, 8496); break;
	case 2: standroi = prange(16868, 18944); standroi += 2 * (1 - (standroi + prange(18945, 27217))); break;
	case 3: standroi = prange(32194, 33427); standroi += 2 * (1 - (standroi + prange(33428, 38342))); break;
	case 4: standroi = prange(41291, 42021); standroi += 2 * (1 - (standroi + prange(42022, 44931))); break;
	case 5: standroi = prange(46733, 47178); standroi += 2 * (1 - (standroi + prange(47179, 48955))); break;
	case 6: standroi = prange(49988, 50243); standroi += 2 * (1 - (standroi + prange(50244, 51260))); break;
	case 7: standroi = prange(51906, 52065); standroi += 2 * (1 - (standroi + prange(52066, 52701))); break;
	case 8: standroi = prange(53089, 53184); standroi += 2 * (1 - (standroi + prange(53185, 53566))); break;
	case 9: standroi = prange(53825, 53888); standroi += 2 * (1 - (standroi + prange(53889, 54143))); break;
	case 10: standroi = prange(54273, 54304); standroi += 2 * (1 - (standroi + prange(54305, 54432))); break;
	}
}

void node::eighteen() {
	switch (upcard){
	case 1: standroi = prange(4721, 5664); standroi += 2 * (1 - (standroi + prange(5665, 8496))); standroi /= 1 - prange(8496, 8496); break;
	case 2: standroi = prange(18954, 21019); standroi += 2 * (1 - (standroi + prange(21020, 27217))); break;
	case 3: standroi = prange(33428, 34660); standroi += 2 * (1 - (standroi + prange(34661, 38342))); break;
	case 4: standroi = prange(42022, 42751); standroi += 2 * (1 - (standroi + prange(42752, 44931))); break;
	case 5: standroi = prange(47179, 47624); standroi += 2 * (1 - (standroi + prange(47625, 48955))); break;
	case 6: standroi = prange(50244, 50498); standroi += 2 * (1 - (standroi + prange(50499, 51260))); break;
	case 7: standroi = prange(52066, 52225); standroi += 2 * (1 - (standroi + prange(52226, 52701))); break;
	case 8: standroi = prange(53185, 53280); standroi += 2 * (1 - (standroi + prange(53281, 53566))); break;
	case 9: standroi = prange(53889, 53952); standroi += 2 * (1 - (standroi + prange(53953, 54143))); break;
	case 10: standroi = prange(54305, 54336); standroi += 2 * (1 - (standroi + prange(54337, 54432))); break;
	}
}

void node::nineteen() {
	switch (upcard){
	case 1: standroi = prange(5665, 6608); standroi += 2 * (1 - (standroi + prange(6609, 8496))); standroi /= 1 - prange(8496, 8496); break;
	case 2: standroi = prange(21020, 23091); standroi += 2 * (1 - (standroi + prange(23092, 27217))); break;
	case 3: standroi = prange(34661, 35891); standroi += 2 * (1 - (standroi + prange(35892, 38342))); break;
	case 4: standroi = prange(42752, 43480); standroi += 2 * (1 - (standroi + prange(43481, 44931))); break;
	case 5: standroi = prange(47625, 48069); standroi += 2 * (1 - (standroi + prange(48070, 48955))); break;
	case 6: standroi = prange(50499, 50753); standroi += 2 * (1 - (standroi + prange(50754, 51260))); break;
	case 7: standroi = prange(52226, 52384); standroi += 2 * (1 - (standroi + prange(52385, 52701))); break;
	case 8: standroi = prange(53281, 53376); standroi += 2 * (1 - (standroi + prange(53377, 53566))); break;
	case 9: standroi = prange(53953, 54016); standroi += 2 * (1 - (standroi + prange(54017, 54143))); break;
	case 10: standroi = prange(54337, 54368); standroi += 2 * (1 - (standroi + prange(54369, 54432))); break;
	}
}

void node::twenty() {
	switch (upcard){
	case 1: standroi = prange(6609, 7552); standroi += 2 * (1 - (standroi + prange(7553, 8496))); standroi /= 1 - prange(8496, 8496); break;
	case 2: standroi = prange(23092, 25158); standroi += 2 * (1 - (standroi + prange(25159, 27217))); break;
	case 3: standroi = prange(35892, 37119); standroi += 2 * (1 - (standroi + prange(37120, 38342))); break;
	case 4: standroi = prange(43481, 44207); standroi += 2 * (1 - (standroi + prange(44208, 44931))); break;
	case 5: standroi = prange(48070, 48513); standroi += 2 * (1 - (standroi + prange(48514, 48955))); break;
	case 6: standroi = prange(50754, 51007); standroi += 2 * (1 - (standroi + prange(51008, 51260))); break;
	case 7: standroi = prange(52385, 52543); standroi += 2 * (1 - (standroi + prange(52544, 52701))); break;
	case 8: standroi = prange(53377, 53471); standroi += 2 * (1 - (standroi + prange(53472, 53566))); break;
	case 9: standroi = prange(54017, 54080); standroi += 2 * (1 - (standroi + prange(54081, 54143))); break;
	case 10: standroi = prange(54369, 54400); standroi += 2 * (1 - (standroi + prange(54401, 54432))); break;
	}
}

void node::twentyone() {
	switch (upcard){
	case 1: standroi = prange(7553, 8495); standroi += 2 * (1 - (standroi + prange(8496, 8496))); standroi /= 1 - prange(8496, 8496); break; //I DON'T THHINK THIS IS CORRECT.
	case 2: standroi = prange(25159, 27217); standroi += 2 * (1 - standroi); break;
	case 3: standroi = prange(37120, 38342); standroi += 2 * (1 - standroi); break;
	case 4: standroi = prange(44208, 44931); standroi += 2 * (1 - standroi); break;
	case 5: standroi = prange(48514, 48955); standroi += 2 * (1 - standroi); break;
	case 6: standroi = prange(51008, 51260); standroi += 2 * (1 - standroi); break;
	case 7: standroi = prange(52544, 52701); standroi += 2 * (1 - standroi); break;
	case 8: standroi = prange(53472, 53566); standroi += 2 * (1 - standroi); break;
	case 9: standroi = prange(54081, 54143); standroi += 2 * (1 - standroi); break;
	case 10: standroi = prange(54401, 54431); standroi += 2 * (1 - (standroi + prange(54432, 54432))); break;
	}
}

void node::blackjack() {
	switch (upcard){
	case 10: standroi = prange(54432, 54432); standroi = 2.5 * (1 - standroi); break;
	default: standroi = 2.5; break; //Dealer checks for blackjack with ace. The return will be 2.5 without a blackjack.
	}
}
