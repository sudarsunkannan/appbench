
struct gt_spinlock_t {
	int locked;
	long tid_holder;
};

extern int gt_spinlock_init(struct gt_spinlock_t* spinlock);
extern int gt_spin_lock(struct gt_spinlock_t* spinlock);
extern int gt_spin_unlock(struct gt_spinlock_t *spinlock);

