The files:
pi.py:
	This is the code that runs on the garbage collector.
	It listens for incoming UDP packets from the kerrigan client with
	the format "engine,value" where engine can be any value
	between a and d and value can be any value between -250 and 250.
	
	It simply handles these packets by setting the speed of the given
	engine to the given value. Pretty bad considering the fact
	that if we lose connection while one the engines is on, it will stay
	on. Also, we need to add encryption.

ds3.py:
	This is the code that runs on the the laptop. It handles input
	from the PS3 controller, calculates engine values and then sends
	these values to the kerrigan server via UDP.

	It handles input by simply polling the controller axes (axes are
	values between -1 to 1 or 0 to 1. There are about 10-20 different
	axes. Each axis belongs to a different controller button.
	
	The axis value represents how "hard" the user pressed the button
	or how far he moved the stick in a specific direction.

	For example, one axis gets the value -1 when the left stick is
	as low as it can go, and the value 1 when the left stick is as
	high as it can go. The axis will get the value 0 when the left
	stick is untouched or it is simply moved left or right without
	being moved up or down.

	Another example is an axis that gets the value 1 when the square
	button is pressed very hard, and -0.5 when it isn't pressed as hard.

	In the program I polled the important axis (R2, L2, Left stick 
	horizontal axis, Right stick horizontal axis, square and circle).

	All these axis values are passed to the handleaxis function where
	engine values are calculated accordingly and then sent to the
	garbage collector.
