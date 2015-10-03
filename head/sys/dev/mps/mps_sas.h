/*-
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2015 Avago Technologies
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Avago Technologies (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

enum {
	MPSSAS_RECOVERY_ABORT		= 1,
	MPSSAS_RECOVERY_LUN_RESET	= 2,
	MPSSAS_RECOVERY_TARGET_RESET	= 3,
	MPSSAS_RECOVERY_REINIT		= 4
};

struct mps_fw_event_work;

struct mpssas_lun {
	SLIST_ENTRY(mpssas_lun) lun_link;
	lun_id_t	lun_id;
	uint32_t	eedp_block_size;
	uint8_t		eedp_formatted;
};

struct mpssas_target {
	uint64_t	devname;
	uint32_t	devinfo;
	uint16_t	handle;
	uint16_t	encl_handle;
	uint16_t	encl_slot;
	u_int		frozen;
	uint8_t		linkrate;
	uint8_t		flags;
#define MPSSAS_TARGET_INUNKNOWN		0x01
#define MPSSAS_TARGET_INABORT		0x02
#define MPSSAS_TARGET_INRESET		0x03
#define MPSSAS_TARGET_INDIAGRESET	0x04
#define MPSSAS_TARGET_INREMOVAL		0x05
#define MPSSAS_TARGET_INRECOVERY	0x0F
#define MPS_TARGET_FLAGS_RAID_COMPONENT 0x10
#define MPS_TARGET_FLAGS_VOLUME         0x20
#define MPS_TARGET_IS_SATA_SSD		0x40

	uint16_t	tid;
	SLIST_HEAD(, mpssas_lun) luns;
	struct mps_command *tm;
	TAILQ_HEAD(, mps_command) timedout_commands;
	struct mtx	tmtx;
	uint16_t        exp_dev_handle;
	uint16_t        phy_num;
	uint16_t	parent_handle;
	uint64_t	sasaddr;
	uint64_t	parent_sasaddr;
	uint32_t	parent_devinfo;
	struct sysctl_ctx_list sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
	TAILQ_ENTRY(mpssas_target) sysctl_link;
	unsigned int    outstanding;
	unsigned int    timeouts;
	unsigned int    aborts;
	unsigned int    logical_unit_resets;
	unsigned int    target_resets;
	uint8_t		stop_at_shutdown;
	uint8_t		supports_SSU;
	char		mtxname[8];
};

struct mpssas_softc {
	struct mps_softc	*sc;
	u_int			flags;
#define MPSSAS_IN_DISCOVERY	(1 << 0)
#define MPSSAS_IN_STARTUP	(1 << 1)
#define MPSSAS_DISCOVERY_TIMEOUT_PENDING	(1 << 2)
#define	MPSSAS_SHUTDOWN		(1 << 4)
	u_int			qfrozen;
	u_int			maxtargets;
	struct mpssas_target	*targets;
	struct cam_devq		*devq;
	struct cam_sim		*sim;
	struct cam_path		*path;
	struct intr_config_hook	sas_ich;
	struct callout		discovery_callout;
	struct mps_event_handle	*mpssas_eh;

	u_int                   startup_refcount;
	struct proc             *sysctl_proc;

	struct taskqueue	*ev_tq;
	struct task		ev_task;
	TAILQ_HEAD(, mps_fw_event_work)	ev_queue;
};

MALLOC_DECLARE(M_MPSSAS);

/*
 * Abstracted so that the driver can be backwards and forwards compatible
 * with future versions of CAM that will provide this functionality.
 */
#define MPS_SET_LUN(lun, ccblun)	\
	mpssas_set_lun(lun, ccblun)

static __inline int
mpssas_set_lun(uint8_t *lun, u_int ccblun)
{
	uint64_t *newlun;

	newlun = (uint64_t *)lun;
	*newlun = 0;
	if (ccblun <= 0xff) {
		/* Peripheral device address method, LUN is 0 to 255 */
		lun[1] = ccblun;
	} else if (ccblun <= 0x3fff) {
		/* Flat space address method, LUN is <= 16383 */
		scsi_ulto2b(ccblun, lun);
		lun[0] |= 0x40;
	} else if (ccblun <= 0xffffff) {
		/* Extended flat space address method, LUN is <= 16777215 */
		scsi_ulto3b(ccblun, &lun[1]);
		/* Extended Flat space address method */
		lun[0] = 0xc0;
		/* Length = 1, i.e. LUN is 3 bytes long */
		lun[0] |= 0x10;
		/* Extended Address Method */
		lun[0] |= 0x02;
	} else {
		return (EINVAL);
	}

	return (0);
}

#define mpssas_set_ccbstatus(ccb, sts)			\
do {							\
	(ccb)->ccb_h.status &= ~CAM_STATUS_MASK;	\
	(ccb)->ccb_h.status |= (sts);			\
} while (0)

#define mpssas_get_ccbstatus(ccb)			\
	((ccb)->ccb_h.status & CAM_STATUS_MASK)

#define MPS_SET_SINGLE_LUN(req, lun)	\
do {					\
	bzero((req)->LUN, 8);		\
	(req)->LUN[1] = lun;		\
} while(0)

#define mpssas_lock_target(t)		mtx_lock(&(t)->tmtx)
#define mpssas_unlock_target(t)		mtx_unlock(&(t)->tmtx)
#define mpssas_trylock_target(t)	mtx_trylock(&(t)->tmtx)

void mpssas_rescan_target(struct mps_softc *sc, struct mpssas_target *targ);
void mpssas_discovery_end(struct mpssas_softc *sassc);
void mpssas_prepare_for_tm(struct mps_softc *sc, struct mps_command *tm,
    struct mpssas_target *target, lun_id_t lun_id);
void mpssas_startup_increment(struct mpssas_softc *sassc);
void mpssas_startup_decrement(struct mpssas_softc *sassc);

void mpssas_firmware_event_work(void *arg, int pending);
int mpssas_check_id(struct mpssas_softc *sassc, int id);
void mpssas_record_event(struct mps_softc *sc,
    MPI2_EVENT_NOTIFICATION_REPLY *event_reply);
void mpssas_ir_shutdown(struct mps_softc *sc);
void mpssas_handle_reinit(struct mps_softc *sc);
void mpssas_evt_handler(struct mps_softc *sc, uintptr_t data,
    MPI2_EVENT_NOTIFICATION_REPLY *event);
void mpssas_prepare_remove(struct mpssas_softc *sassc, uint16_t handle);
void mpssas_prepare_volume_remove(struct mpssas_softc *sassc, uint16_t handle);
int mpssas_startup(struct mps_softc *sc);
struct mpssas_target * mpssas_find_target_by_handle(struct mpssas_softc *,
    int, uint16_t);
void mpssas_realloc_targets(struct mps_softc *sc, int maxtargets);
struct mps_command * mpssas_alloc_tm(struct mps_softc *sc);
void mpssas_free_tm(struct mps_softc *sc, struct mps_command *tm);
void mpssas_release_simq_reinit(struct mpssas_softc *sassc);
int mpssas_send_reset(struct mps_softc *sc, struct mps_command *tm, int type);

