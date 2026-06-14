c compile :
	make -C kern/build
	make -C user/build

QEMU = qemu-system-i386

r run :
	$(QEMU) -kernel kern/build/hypervisor -initrd user/build/user.nova -serial stdio -display none

cl clean :
	make -C kern/build clean
	make -C user/build clean

cla cleanall : clean
	make -C kern/build cleanall
	make -C user/build cleanall
