/*
Some code has been borrowed from the Linux kernel which is under GPLv2 license.
2017 Modified for Magenta by Slava Imameev.
*/
#pragma once

#include <magenta/compiler.h>
#include <arch/riscv/asm/constant.h>
#include <arch/riscv/asm/va-space-layout.h>
#include <arch/riscv/pgtable-bits.h>

__BEGIN_CDECLS

#ifndef __ASSEMBLY__

/* Page Upper Directory not used in RISC-V */
#include <arch/riscv/pgtable-nopud.h>
#include <arch/riscv/page.h>

#ifdef CONFIG_64BIT
#include <arch/riscv/pgtable-64.h>
#else
#include <arch/riscv/pgtable-32.h>
#endif /* CONFIG_64BIT */

/* Number of entries in the page global directory */
#define PTRS_PER_PGD    (PAGE_SIZE / sizeof(pgd_t))
/* Number of entries in the page table */
#define PTRS_PER_PTE    (PAGE_SIZE / sizeof(pte_t))

/* Number of PGD entries that a user-mode program can use */
#define USER_PTRS_PER_PGD   (USER_TASK_SIZE / PGDIR_SIZE)
#define FIRST_USER_ADDRESS  0

/*number of kernel space entries in pgd*/
#define KERNEL_PTRS_PER_PGD   (PTRS_PER_PGD - USER_PTRS_PER_PGD)

/* Page protection bits */
#define _PAGE_BASE	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_USER)

#define PAGE_NONE		__pgprot(0)
#define PAGE_READ		__pgprot(_PAGE_BASE | _PAGE_READ)
#define PAGE_WRITE		__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_WRITE)
#define PAGE_EXEC		__pgprot(_PAGE_BASE | _PAGE_EXEC)
#define PAGE_READ_EXEC		__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_EXEC)
#define PAGE_WRITE_EXEC		__pgprot(_PAGE_BASE | _PAGE_READ |	\
					 _PAGE_EXEC | _PAGE_WRITE)

#define PAGE_COPY		PAGE_READ
#define PAGE_COPY_EXEC		PAGE_EXEC
#define PAGE_COPY_READ_EXEC	PAGE_READ_EXEC
#define PAGE_SHARED		PAGE_WRITE
#define PAGE_SHARED_EXEC	PAGE_WRITE_EXEC

#define PAGE_KERNEL		__pgprot(_PAGE_READ | _PAGE_WRITE |	\
					 _PAGE_PRESENT | _PAGE_ACCESSED)

#define swapper_pg_dir NULL

/* MAP_PRIVATE permissions: xwr (copy-on-write) */
#define __P000	PAGE_NONE
#define __P001	PAGE_READ
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_EXEC
#define __P101	PAGE_READ_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_READ_EXEC

/* MAP_SHARED permissions: xwr */
#define __S000	PAGE_NONE
#define __S001	PAGE_READ
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_EXEC
#define __S101	PAGE_READ_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC

/*
 * ZERO_PAGE is a global shared page that is always zero,
 * used for zero-mapped memory areas, etc.
 */
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

//
// general page table routines
//

/* Yields the page frame number (PFN) of a page table entry
   This is applied to any level page table - pgd, pud, pmd, pte.
   The name is a misnomer as pte is usually used for the leaf
   page table entry but the RISC-V documentation refers any 
   level page table entry as pte.
*/
static inline unsigned long pte_pfn(pte_t pte)
{
	return (pte_val(pte) >> _PAGE_PFN_SHIFT);
}

#define pte_to_phys(x)     pfn_to_phys(pte_pfn(x))

//
// type casting to calm the compiler
//
static inline paddr_t pgd_to_phys(pgd_t x)
{
	pte_t  pte = {.pte = pgd_val(x)}; 
  	return pte_to_phys(pte);
}

static inline paddr_t pud_to_phys(pud_t x)
{
	//
	// there is no pud table, pud is mapped to pgd
	//
	pte_t  pte = {.pte = pud_val(x) }; 
  	return pte_to_phys(pte);
}

static inline paddr_t pmd_to_phys(pmd_t x)
{
	pte_t  pte = {.pte = pmd_val(x)}; 
  	return pte_to_phys(pte);
}

#define pgd_all_flags(pgd, flags) (flags == (pgd_val(pgd) & flags))
#define pud_all_flags(pud, flags) (flags == (pud_val(pud) & flags))
#define pmd_all_flags(pmd, flags) (flags == (pmd_val(pmd) & flags))
#define pte_all_flags(pte, flags) (flags == (pte_val(pte) & flags))

#define pgd_any_flags(pgd, flags) (0x0 != (pgd_val(pgd) & flags))
#define pud_any_flags(pud, flags) (0x0 != (pud_val(pud) & flags))
#define pmd_any_flags(pmd, flags) (0x0 != (pmd_val(pmd) & flags))
#define pte_any_flags(pte, flags) (0x0 != (pte_val(pte) & flags))

//
// specific page table level routines
//

static inline bool pmd_present(pmd_t pmd)
{
	return pmd_all_flags(pmd, _PAGE_PRESENT);
}

static inline bool pmd_none(pmd_t pmd)
{
	return (pmd_val(pmd) == 0);
}

static inline bool pmd_bad(pmd_t pmd)
{
	return !pmd_present(pmd);
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

static inline bool pmd_huge(pmd_t pmd)
{
	return pmd_present(pmd)
		&& pmd_any_flags(pmd, (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC));
}

#define pgd_index(addr) (((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))

/* Locate an entry in the page global directory */
static inline pgd_t *pgd_offset(pgd_t* pgd, unsigned long addr)
{
	return pgd + pgd_index(addr);
}
/* Locate an entry in the kernel page global directory */
#define pgd_offset_k(addr)      pgd_offset(kernel_init_pgd, (addr))

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)pfn_to_virt(pmd_val(pmd) >> _PAGE_PFN_SHIFT);
}

/* Constructs a page table entry */
static inline pte_t pfn_pte(unsigned long pfn, pgprot_t prot)
{
	return __pte((pfn << _PAGE_PFN_SHIFT) | pgprot_val(prot));
}

#define pte_index(addr) (((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)) 

static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long addr)
{
	return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(addr);
}

#define pte_offset_map(dir, addr)	pte_offset_kernel((dir), (addr))
#define pte_offset(dir, addr) pte_offset_map(dir, addr)
#define pte_unmap(pte)			((void)(pte))

/*
 * Certain architectures need to do special things when PTEs within
 * a page table are directly modified.  Thus, the following hook is
 * made available.
 */
static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

static inline bool pte_present(pte_t pte)
{
	return pte_all_flags(pte, _PAGE_PRESENT);
}

static inline void pte_set_none(pte_t *ptep)
{
    set_pte(ptep, __pte(0x0));
}

static inline bool pte_none(pte_t pte)
{
	return (pte_val(pte) == 0);
}

/* static inline int pte_read(pte_t pte) */

static inline bool pte_read(pte_t pte)
{
	return pte_all_flags(pte, _PAGE_READ);
}

static inline bool pte_write(pte_t pte)
{
	return pte_all_flags(pte, _PAGE_WRITE);
}

static inline bool pte_exec(pte_t pte)
{
	return pte_all_flags(pte, _PAGE_EXEC);
}

static inline bool pte_user(pte_t pte)
{
	return pte_all_flags(pte, _PAGE_USER);
}

static inline int pte_huge(pte_t pte)
{
	return pte_present(pte)
		&& pte_any_flags(pte, (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC));
}

/*
mapped leaf contains valid virt to physical mappinf for a va
*/
static inline int pte_valid_va2pa(pte_t pte)
{
	return pte_present(pte)
		&& pte_any_flags(pte, (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC));
}

/*a leaf pte points to a page with data or to nothing(i.e. no mappng),
  non-leaf pte points to next level page table*/
static inline bool pte_leaf(pte_t pte)
{
	return !pte_present(pte) ||
		   pte_any_flags(pte, (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC));
}

/* static inline int pte_exec(pte_t pte) */

static inline bool pte_dirty(pte_t pte)
{
	return pte_all_flags(pte, _PAGE_DIRTY);
}

static inline bool pte_young(pte_t pte)
{
	return pte_all_flags(pte, _PAGE_ACCESSED);
}

static inline bool pte_special(pte_t pte)
{
	return pte_all_flags(pte, _PAGE_SPECIAL);
}

/* static inline pte_t pte_rdprotect(pte_t pte) */

static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_PAGE_WRITE));
}

/* static inline pte_t pte_mkread(pte_t pte) */

static inline pte_t pte_mkwrite(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_WRITE);
}

/* static inline pte_t pte_mkexec(pte_t pte) */

static inline pte_t pte_mkdirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_DIRTY);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_PAGE_DIRTY));
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_PAGE_ACCESSED));
}

static inline pte_t pte_mkold(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_ACCESSED);
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SPECIAL);
}

/* Modify page protection bits */
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

static inline pte_t pte_modify_access(pte_t pte, pgprot_t newprot)
{
	assert( 0x0 == (_PAGE_CHG_ACCESS_MASK & pgprot_val(newprot)) );
	return __pte((pte_val(pte) & _PAGE_CHG_ACCESS_MASK) | pgprot_val(newprot));
}

#define pgd_ERROR(e) \
	panic("%s:%d: bad pgd " PTE_FMT ".\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Encode and decode a swap entry
 *
 * Format of swap PTE:
 *	bit            0:	_PAGE_PRESENT (zero)
 *	bit            1:	reserved for future use (zero)
 *	bits      2 to 6:	swap type
 *	bits 7 to XLEN-1:	swap offset
 */
#define __SWP_TYPE_SHIFT	2
#define __SWP_TYPE_BITS		5
#define __SWP_TYPE_MASK		((1UL << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT	(__SWP_TYPE_BITS + __SWP_TYPE_SHIFT)

#define MAX_SWAPFILES_CHECK()	\
	BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

#define __swp_type(x)		(((x).val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK)
#define __swp_offset(x)		((x).val >> __SWP_OFFSET_SHIFT)
#define __swp_entry(type, offset) ((swp_entry_t) \
	{ ((type) << __SWP_TYPE_SHIFT) | ((offset) << __SWP_OFFSET_SHIFT) })

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

/* Task size is 0x40000000000(256GB) for RV64 or 0xb800000 for RV32.
   Note that PGDIR_SIZE must evenly divide USER_TASK_SIZE. */
#ifdef CONFIG_64BIT
	#define USER_TASK_SIZE (PGDIR_SIZE * PTRS_PER_PGD / 2)
#else
	#error "32 bit CPU not supported in the current release"
	#define USER_TASK_SIZE 0xb800000
#endif

//
// check that the highest user VA fits in the bottom
// half of the GDP
//
static_assert(USER_ASPACE_SIZE <= USER_TASK_SIZE, "USER_ASPACE_SIZE <= USER_TASK_SIZE");

//
// returns current CPU sptbr register value translated to virtual address
//
pgd_t* get_current_sptbr_va(void);

extern pgd_t* kernel_init_pgd;

#endif /* !__ASSEMBLY__ */

__END_CDECLS

