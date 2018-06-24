class Test1
	def initialize(v)
		puts"init"
		@ival = v
	end
	def show
		puts @ival
	end
end


obj1=Test1.new "object1"
obj2=Test1.new "object2"

obj1.show
obj2.show

