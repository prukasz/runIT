First read concept if unsure ask. 
I dont know how to make EN and ENO seamless for user - so he uses if only wants
help me with thise one settled we move to implementation 
 

First global variable accessor:
- let it be a short rectangle with name of accessed
- if to long - truncated to last important path like ...temperature 
- when clicked then automatically right screen pops out to edit if requires manual. 
- if drag n drop is enough - it should be enough to drag from folders and varaibles and put on block 
- when hovered over block during wiring - there appears zoom over block with pin to chose or EN 
- IN can be selectable both by dropping object on enlarged pin or by clicking on pin in enlarged view

- when enlarged view displays few pins - only compatible type is allowed (number and bool are casted - so allowed ) but ptr and string is not capable 

- you can drop empty accessor from top toolbar - that way every pin is allowed to drop on but still end result must be compatible

- if possible accessor should then snap to block - so no wire but still if required can be moved out with an wire


Then ENO and EN:

- ENO is Green stripe 
- EN is Red stripe

- When output of block is connected to input of any - then ENO is automatically - linked 

Then canvas marking:

- Every block in for loop should be included as grouped in smoke like area - contour - those then can overlap with eachother. each of group are shold posess slight coloru tint
- same thing for IF block and SWITCH. this will visually help understand code




