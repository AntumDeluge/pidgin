/**
 * @file certificate.h Public-Key Certificate API
 * @ingroup core
 * @see @ref certificate-signals
 */

/*
 *
 * purple
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#ifndef _PURPLE_CERTIFICATE_H
#define _PURPLE_CERTIFICATE_H

#include <time.h>

#include <glib.h>

typedef enum
{
	PURPLE_CERTIFICATE_UNKNOWN_ERROR = -1,

	/* Not an error */
	PURPLE_CERTIFICATE_VALID = 0,

	/* Non-fatal */
	PURPLE_CERTIFICATE_NON_FATALS_MASK = 0x0000FFFF,

	/* The certificate is self-signed. */
	PURPLE_CERTIFICATE_SELF_SIGNED = 0x01,

	/* The CA is not in libpurple's pool of certificates. */
	PURPLE_CERTIFICATE_CA_UNKNOWN = 0x02,

	/* The current time is before the certificate's specified
	 * activation time.
	 */
	PURPLE_CERTIFICATE_NOT_ACTIVATED = 0x04,

	/* The current time is after the certificate's specified expiration time */
	PURPLE_CERTIFICATE_EXPIRED = 0x08,

	/* The certificate's subject name doesn't match the expected */
	PURPLE_CERTIFICATE_NAME_MISMATCH = 0x10,

	/* No CA pool was found. This shouldn't happen... */
	PURPLE_CERTIFICATE_NO_CA_POOL = 0x20,

	/* Fatal */
	PURPLE_CERTIFICATE_FATALS_MASK = 0xFFFF0000,

	/* The signature chain could not be validated. Due to limitations in the
	 * the current API, this also indicates one of the CA certificates in the
	 * chain is expired (or not yet activated). FIXME 3.0.0 */
	PURPLE_CERTIFICATE_INVALID_CHAIN = 0x10000,

	/* The signature has been revoked. */
	PURPLE_CERTIFICATE_REVOKED = 0x20000,

	/* The certificate was rejected by the user. */
	PURPLE_CERTIFICATE_REJECTED = 0x40000,

	PURPLE_CERTIFICATE_LAST = 0x80000,
} PurpleCertificateVerificationStatus;

#define PURPLE_TYPE_CERTIFICATE   (purple_certificate_get_type())
typedef struct _PurpleCertificate PurpleCertificate;

#define PURPLE_TYPE_CERTIFICATE_POOL  (purple_certificate_pool_get_type())
typedef struct _PurpleCertificatePool PurpleCertificatePool;

typedef struct _PurpleCertificateScheme PurpleCertificateScheme;
typedef struct _PurpleCertificateVerifier PurpleCertificateVerifier;
typedef struct _PurpleCertificateVerificationRequest PurpleCertificateVerificationRequest;

/**
 * Callback function for the results of a verification check
 * @st:       Status code
 * @userdata: User-defined data
 */
typedef void (*PurpleCertificateVerifiedCallback)
		(PurpleCertificateVerificationStatus st,
		 gpointer userdata);

/** A certificate instance
 *
 *  An opaque data structure representing a single certificate under some
 *  CertificateScheme
 */
struct _PurpleCertificate
{
	/** Scheme this certificate is under */
	PurpleCertificateScheme * scheme;
	/** Opaque pointer to internal data */
	gpointer data;
};

/**
 * Database for retrieval or storage of Certificates
 *
 * More or less a hash table; all lookups and writes are controlled by a string
 * key.
 */
struct _PurpleCertificatePool
{
	/** Scheme this Pool operates for */
	gchar *scheme_name;
	/** Internal name to refer to the pool by */
	gchar *name;

	/** User-friendly name for this type
	 *  ex: N_("SSL Servers")
	 *  When this is displayed anywhere, it should be i18ned
	 *  ex: _(pool->fullname)
	 */
	gchar *fullname;

	/** Internal pool data */
	gpointer data;

	/**
	 * Set up the Pool's internal state
	 *
	 * Upon calling purple_certificate_register_pool() , this function will
	 * be called. May be NULL.
	 * Returns: TRUE if the initialization succeeded, otherwise FALSE
	 */
	gboolean (* init)(void);

	/**
	 * Uninit the Pool's internal state
	 *
	 * Will be called by purple_certificate_unregister_pool() . May be NULL
	 */
	void (* uninit)(void);

	/** Check for presence of a certificate in the pool using unique ID */
	gboolean (* cert_in_pool)(const gchar *id);
	/** Retrieve a PurpleCertificate from the pool */
	PurpleCertificate * (* get_cert)(const gchar *id);
	/** Add a certificate to the pool. Must overwrite any other
	 *  certificates sharing the same ID in the pool.
	 *  Returns: TRUE if the operation succeeded, otherwise FALSE
	 */
	gboolean (* put_cert)(const gchar *id, PurpleCertificate *crt);
	/** Delete a certificate from the pool */
	gboolean (* delete_cert)(const gchar *id);

	/** Returns a list of IDs stored in the pool */
	GList * (* get_idlist)(void);

	/*< private >*/
	void (*_purple_reserved1)(void);
	void (*_purple_reserved2)(void);
	void (*_purple_reserved3)(void);
	void (*_purple_reserved4)(void);
};

/** A certificate type
 *
 *  A CertificateScheme must implement all of the fields in the structure,
 *  and register it using purple_certificate_register_scheme()
 *
 *  There may be only ONE CertificateScheme provided for each certificate
 *  type, as specified by the "name" field.
 */
struct _PurpleCertificateScheme
{
	/** Name of the certificate type
	 *  ex: "x509", "pgp", etc.
	 *  This must be globally unique - you may not register more than one
	 *  CertificateScheme of the same name at a time.
	 */
	gchar * name;

	/** User-friendly name for this type
	 *  ex: N_("X.509 Certificates")
	 *  When this is displayed anywhere, it should be i18ned
	 *  ex: _(scheme->fullname)
	 */
	gchar * fullname;

	/** Imports a certificate from a file
	 *
	 *  @filename:   File to import the certificate from
	 *  Returns:           Pointer to the newly allocated Certificate struct
	 *                    or NULL on failure.
	 */
	PurpleCertificate * (* import_certificate)(const gchar * filename);

	/**
	 * Exports a certificate to a file
	 *
	 * @filename:    File to export the certificate to
	 * @crt:         Certificate to export
	 * Returns: TRUE if the export succeeded, otherwise FALSE
	 * @see purple_certificate_export()
	 */
	gboolean (* export_certificate)(const gchar *filename, PurpleCertificate *crt);

	/**
	 * Duplicates a certificate
	 *
	 * Certificates are generally assumed to be read-only, so feel free to
	 * do any sort of reference-counting magic you want here. If this ever
	 * changes, please remember to change the magic accordingly.
	 * Returns: Reference to the new copy
	 */
	PurpleCertificate * (* copy_certificate)(PurpleCertificate *crt);

	/** Destroys and frees a Certificate structure
	 *
	 *  Destroys a Certificate's internal data structures and calls
	 *  free(crt)
	 *
	 *  @crt:  Certificate instance to be destroyed. It WILL NOT be
	 *              destroyed if it is not of the correct
	 *              CertificateScheme. Can be NULL
	 */
	void (* destroy_certificate)(PurpleCertificate * crt);

	/** Find whether "crt" has a valid signature from issuer "issuer"
	 *  @see purple_certificate_signed_by() */
	gboolean (*signed_by)(PurpleCertificate *crt, PurpleCertificate *issuer);
	/**
	 * Retrieves the certificate public key fingerprint using SHA1
	 *
	 * @crt:   Certificate instance
	 * Returns: Binary representation of SHA1 hash - must be freed using
	 *         g_byte_array_free()
	 */
	GByteArray * (* get_fingerprint_sha1)(PurpleCertificate *crt);

	/**
	 * Retrieves a unique certificate identifier
	 *
	 * @crt:   Certificate instance
	 * Returns: Newly allocated string that can be used to uniquely
	 *         identify the certificate.
	 */
	gchar * (* get_unique_id)(PurpleCertificate *crt);

	/**
	 * Retrieves a unique identifier for the certificate's issuer
	 *
	 * @crt:   Certificate instance
	 * Returns: Newly allocated string that can be used to uniquely
	 *         identify the issuer's certificate.
	 */
	gchar * (* get_issuer_unique_id)(PurpleCertificate *crt);

	/**
	 * Gets the certificate subject's name
	 *
	 * For X.509, this is the "Common Name" field, as we're only using it
	 * for hostname verification at the moment
	 *
	 * @see purple_certificate_get_subject_name()
	 *
	 * @crt:   Certificate instance
	 * Returns: Newly allocated string with the certificate subject.
	 */
	gchar * (* get_subject_name)(PurpleCertificate *crt);

	/**
	 * Check the subject name against that on the certificate
	 * @see purple_certificate_check_subject_name()
	 * Returns: TRUE if it is a match, else FALSE
	 */
	gboolean (* check_subject_name)(PurpleCertificate *crt, const gchar *name);

	/** Retrieve the certificate activation/expiration times */
	gboolean (* get_times)(PurpleCertificate *crt, gint64 *activation, gint64 *expiration);

	/** Imports certificates from a file
	 *
	 *  @filename:   File to import the certificates from
	 *  Returns:           GSList of pointers to the newly allocated Certificate structs
	 *                    or NULL on failure.
	 */
	GSList * (* import_certificates)(const gchar * filename);

	/**
	 * Retrieves the certificate data in DER form
	 *
	 * @crt:   Certificate instance
	 * Returns: Binary DER representation of certificate - must be freed using
	 *         g_byte_array_free()
	 */
	GByteArray * (* get_der_data)(PurpleCertificate *crt);

	/**
	 * Retrieves a string representation of the certificate suitable for display
	 *
	 * @crt:   Certificate instance
	 * Returns: User-displayable string representation of certificate - must be
	 *         freed using g_free().
	 */
	gchar * (* get_display_string)(PurpleCertificate *crt);

	/*< private >*/
	void (*_purple_reserved1)(void);
};

/** A set of operations used to provide logic for verifying a Certificate's
 *  authenticity.
 *
 * A Verifier provider must fill out these fields, then register it using
 * purple_certificate_register_verifier()
 *
 * The (scheme_name, name) value must be unique for each Verifier - you may not
 * register more than one Verifier of the same name for each Scheme
 */
struct _PurpleCertificateVerifier
{
	/** Name of the scheme this Verifier operates on
	 *
	 * The scheme will be looked up by name when a Request is generated
	 * using this Verifier
	 */
	gchar *scheme_name;

	/** Name of the Verifier - case insensitive */
	gchar *name;

	/**
	 * Start the verification process
	 *
	 * To be called from purple_certificate_verify once it has
	 * constructed the request. This will use the information in the
	 * given VerificationRequest to check the certificate and callback
	 * the requester with the verification results.
	 *
	 * @vrq:      Request to process
	 */
	void (* start_verification)(PurpleCertificateVerificationRequest *vrq);

	/**
	 * Destroy a completed Request under this Verifier
	 * The function pointed to here is only responsible for cleaning up
	 * whatever PurpleCertificateVerificationRequest::data points to.
	 * It should not call free(vrq)
	 *
	 * @vrq:       Request to destroy
	 */
	void (* destroy_request)(PurpleCertificateVerificationRequest *vrq);

	/*< private >*/
	void (*_purple_reserved1)(void);
	void (*_purple_reserved2)(void);
	void (*_purple_reserved3)(void);
	void (*_purple_reserved4)(void);
};

/** Structure for a single certificate request
 *
 *  Useful for keeping track of the state of a verification that involves
 *  several steps
 */
struct _PurpleCertificateVerificationRequest
{
	/** Reference to the verification logic used */
	PurpleCertificateVerifier *verifier;
	/** Reference to the scheme used.
	 *
	 * This is looked up from the Verifier when the Request is generated
	 */
	PurpleCertificateScheme *scheme;

	/**
	 * Name to check that the certificate is issued to
	 *
	 * For X.509 certificates, this is the Common Name
	 */
	gchar *subject_name;

	/** List of certificates in the chain to be verified (such as that returned by purple_ssl_get_peer_certificates )
	 *
	 * This is most relevant for X.509 certificates used in SSL sessions.
	 * The list order should be: certificate, issuer, issuer's issuer, etc.
	 */
	GList *cert_chain;

	/** Internal data used by the Verifier code */
	gpointer data;

	/** Function to call with the verification result */
	PurpleCertificateVerifiedCallback cb;
	/** Data to pass to the post-verification callback */
	gpointer cb_data;
};

G_BEGIN_DECLS

/*****************************************************************************/
/** @name Certificate Verification Functions                                 */
/*****************************************************************************/
/*@{*/

/**
 * Constructs a verification request and passed control to the specified Verifier
 *
 * It is possible that the callback will be called immediately upon calling
 * this function. Plan accordingly.
 *
 * @verifier:      Verification logic to use.
 *                      @see purple_certificate_find_verifier()
 *
 * @subject_name:  Name that should match the first certificate in the
 *                      chain for the certificate to be valid. Will be strdup'd
 *                      into the Request struct
 *
 * @cert_chain:    Certificate chain to check. If there is more than one
 *                      certificate in the chain (X.509), the peer's
 *                      certificate comes first, then the issuer/signer's
 *                      certificate, etc. The whole list is duplicated into the
 *                      Request struct.
 *
 * @cb:            Callback function to be called with whether the
 *                      certificate was approved or not.
 * @cb_data:       User-defined data for the above.
 */
void
purple_certificate_verify (PurpleCertificateVerifier *verifier,
			   const gchar *subject_name, GList *cert_chain,
			   PurpleCertificateVerifiedCallback cb,
			   gpointer cb_data);

/**
 * Completes and destroys a VerificationRequest
 *
 * @vrq:           Request to conclude
 * @st:            Success/failure code to pass to the request's
 *                      completion callback.
 */
void
purple_certificate_verify_complete(PurpleCertificateVerificationRequest *vrq,
				   PurpleCertificateVerificationStatus st);

/*@}*/

/*****************************************************************************/
/** @name Certificate Functions                                              */
/*****************************************************************************/
/*@{*/

/**
 * Returns the GType for the PurpleCertificate boxed structure.
 */
GType purple_certificate_get_type(void);

/**
 * Makes a duplicate of a certificate
 *
 * @crt:        Instance to duplicate
 * Returns: Pointer to new instance
 */
PurpleCertificate *
purple_certificate_copy(PurpleCertificate *crt);

/**
 * Duplicates an entire list of certificates
 *
 * @crt_list:   List to duplicate
 * Returns: New list copy
 */
GList *
purple_certificate_copy_list(GList *crt_list);

/**
 * Destroys and free()'s a Certificate
 *
 * @crt:        Instance to destroy. May be NULL.
 */
void
purple_certificate_destroy (PurpleCertificate *crt);

/**
 * Destroy an entire list of Certificate instances and the containing list
 *
 * @crt_list:   List of certificates to destroy. May be NULL.
 */
void
purple_certificate_destroy_list (GList * crt_list);

/**
 * Check whether 'crt' has a valid signature made by 'issuer'
 *
 * @crt:        Certificate instance to check signature of
 * @issuer:     Certificate thought to have signed 'crt'
 *
 * Returns: TRUE if 'crt' has a valid signature made by 'issuer',
 *         otherwise FALSE
 * @todo Find a way to give the reason (bad signature, not the issuer, etc.)
 */
gboolean
purple_certificate_signed_by(PurpleCertificate *crt, PurpleCertificate *issuer);

/**
 * Check that a certificate chain is valid and, if not, the failing certificate.
 *
 * Uses purple_certificate_signed_by() to verify that each PurpleCertificate
 * in the chain carries a valid signature from the next. A single-certificate
 * chain is considered to be valid.
 *
 * @chain:      List of PurpleCertificate instances comprising the chain,
 *                   in the order certificate, issuer, issuer's issuer, etc.
 * @failing:    A pointer to a PurpleCertificate*. If not NULL, if the
 *                   chain fails to validate, this will be set to the
 *                   certificate whose signature could not be validated.
 * Returns: TRUE if the chain is valid. See description.
 */
gboolean
purple_certificate_check_signature_chain(GList *chain,
		PurpleCertificate **failing);

/**
 * Imports a PurpleCertificate from a file
 *
 * @scheme:      Scheme to import under
 * @filename:    File path to import from
 * Returns: Pointer to a new PurpleCertificate, or NULL on failure
 */
PurpleCertificate *
purple_certificate_import(PurpleCertificateScheme *scheme, const gchar *filename);

/**
 * Imports a list of PurpleCertificates from a file
 *
 * @scheme:      Scheme to import under
 * @filename:    File path to import from
 * Returns: Pointer to a GSList of new PurpleCertificates, or NULL on failure
 */
GSList *
purple_certificates_import(PurpleCertificateScheme *scheme, const gchar *filename);

/**
 * Exports a PurpleCertificate to a file
 *
 * @filename:    File to export the certificate to
 * @crt:         Certificate to export
 * Returns: TRUE if the export succeeded, otherwise FALSE
 */
gboolean
purple_certificate_export(const gchar *filename, PurpleCertificate *crt);


/**
 * Retrieves the certificate public key fingerprint using SHA1.
 *
 * @crt:        Certificate instance
 * Returns: Binary representation of the hash. You are responsible for free()ing
 *         this.
 * @see purple_base16_encode_chunked()
 */
GByteArray *
purple_certificate_get_fingerprint_sha1(PurpleCertificate *crt);

/**
 * Get a unique identifier for the certificate
 *
 * @crt:        Certificate instance
 * Returns: String representing the certificate uniquely. Must be g_free()'ed
 */
gchar *
purple_certificate_get_unique_id(PurpleCertificate *crt);

/**
 * Get a unique identifier for the certificate's issuer
 *
 * @crt:        Certificate instance
 * Returns: String representing the certificate's issuer uniquely. Must be
 *         g_free()'ed
 */
gchar *
purple_certificate_get_issuer_unique_id(PurpleCertificate *crt);

/**
 * Gets the certificate subject's name
 *
 * For X.509, this is the "Common Name" field, as we're only using it
 * for hostname verification at the moment
 *
 * @crt:   Certificate instance
 * Returns: Newly allocated string with the certificate subject.
 */
gchar *
purple_certificate_get_subject_name(PurpleCertificate *crt);

/**
 * Check the subject name against that on the certificate
 * @crt:   Certificate instance
 * @name:  Name to check.
 * Returns: TRUE if it is a match, else FALSE
 */
gboolean
purple_certificate_check_subject_name(PurpleCertificate *crt, const gchar *name);

/**
 * Get the expiration/activation times.
 *
 * @crt:          Certificate instance
 * @activation:   Reference to store the activation time at. May be NULL
 *                     if you don't actually want it.
 * @expiration:   Reference to store the expiration time at. May be NULL
 *                     if you don't actually want it.
 * Returns: TRUE if the requested values were obtained, otherwise FALSE.
 */
gboolean
purple_certificate_get_times(PurpleCertificate *crt, gint64 *activation, gint64 *expiration);

/**
 * Retrieves the certificate data in DER form.
 *
 * @crt: Certificate instance
 *
 * Returns: Binary DER representation of the certificate - must be freed using
 *         g_byte_array_free().
 */
GByteArray *
purple_certificate_get_der_data(PurpleCertificate *crt);

/**
 * Retrieves a string suitable for displaying a certificate to the user.
 *
 * @crt: Certificate instance
 *
 * Returns: String representing the certificate that may be displayed to the user
 *         - must be freed using g_free().
 */
char *
purple_certificate_get_display_string(PurpleCertificate *crt);

/*@}*/

/*****************************************************************************/
/** @name Certificate Pool Functions                                         */
/*****************************************************************************/
/*@{*/

/**
 * Returns the GType for the PurpleCertificatePool boxed structure.
 * TODO Boxing of PurpleCertificatePool is a temporary solution to having a
 *      GType for certificate pools. This should rather be a GObject instead of
 *      a GBoxed.
 */
GType purple_certificate_pool_get_type(void);

/**
 * Helper function for generating file paths in ~/.purple/certificates for
 * CertificatePools that use them.
 *
 * All components will be escaped for filesystem friendliness.
 *
 * @pool:   CertificatePool to build a path for
 * @id:     Key to look up a Certificate by. May be NULL.
 * Returns: A newly allocated path of the form
 *         ~/.purple/certificates/scheme_name/pool_name/unique_id
 */
gchar *
purple_certificate_pool_mkpath(PurpleCertificatePool *pool, const gchar *id);

/**
 * Determines whether a pool can be used.
 *
 * Checks whether the associated CertificateScheme is loaded.
 *
 * @pool:   Pool to check
 *
 * Returns: TRUE if the pool can be used, otherwise FALSE
 */
gboolean
purple_certificate_pool_usable(PurpleCertificatePool *pool);

/**
 * Looks up the scheme the pool operates under
 *
 * @pool:   Pool to get the scheme of
 *
 * Returns: Pointer to the pool's scheme, or NULL if it isn't loaded.
 * @see purple_certificate_pool_usable()
 */
PurpleCertificateScheme *
purple_certificate_pool_get_scheme(PurpleCertificatePool *pool);

/**
 * Check for presence of an ID in a pool.
 * @pool:   Pool to look in
 * @id:     ID to look for
 * Returns: TRUE if the ID is in the pool, else FALSE
 */
gboolean
purple_certificate_pool_contains(PurpleCertificatePool *pool, const gchar *id);

/**
 * Retrieve a certificate from a pool.
 * @pool:   Pool to fish in
 * @id:     ID to look up
 * Returns: Retrieved certificate, or NULL if it wasn't there
 */
PurpleCertificate *
purple_certificate_pool_retrieve(PurpleCertificatePool *pool, const gchar *id);

/**
 * Add a certificate to a pool
 *
 * Any pre-existing certificate of the same ID will be overwritten.
 *
 * @pool:   Pool to add to
 * @id:     ID to store the certificate with
 * @crt:    Certificate to store
 * Returns: TRUE if the operation succeeded, otherwise FALSE
 */
gboolean
purple_certificate_pool_store(PurpleCertificatePool *pool, const gchar *id, PurpleCertificate *crt);

/**
 * Remove a certificate from a pool
 *
 * @pool:   Pool to remove from
 * @id:     ID to remove
 * Returns: TRUE if the operation succeeded, otherwise FALSE
 */
gboolean
purple_certificate_pool_delete(PurpleCertificatePool *pool, const gchar *id);

/**
 * Get the list of IDs currently in the pool.
 *
 * @pool:   Pool to enumerate
 * Returns: GList pointing to newly-allocated id strings. Free using
 *         purple_certificate_pool_destroy_idlist()
 */
GList *
purple_certificate_pool_get_idlist(PurpleCertificatePool *pool);

/**
 * Destroys the result given by purple_certificate_pool_get_idlist()
 *
 * @idlist: ID List to destroy
 */
void
purple_certificate_pool_destroy_idlist(GList *idlist);

/*@}*/

/*****************************************************************************/
/** @name Certificate Subsystem API                                          */
/*****************************************************************************/
/*@{*/

/**
 * Initialize the certificate system
 */
void
purple_certificate_init(void);

/**
 * Un-initialize the certificate system
 */
void
purple_certificate_uninit(void);

/**
 * Get the Certificate subsystem handle for signalling purposes
 */
gpointer
purple_certificate_get_handle(void);

/** Look up a registered CertificateScheme by name
 * @name:   The scheme name. Case insensitive.
 * Returns: Pointer to the located Scheme, or NULL if it isn't found.
 */
PurpleCertificateScheme *
purple_certificate_find_scheme(const gchar *name);

/**
 * Get all registered CertificateSchemes
 *
 * Returns: GList pointing to all registered CertificateSchemes . This value
 *         is owned by libpurple
 */
GList *
purple_certificate_get_schemes(void);

/** Register a CertificateScheme with libpurple
 *
 * No two schemes can be registered with the same name; this function enforces
 * that.
 *
 * @scheme:  Pointer to the scheme to register.
 * Returns: TRUE if the scheme was successfully added, otherwise FALSE
 */
gboolean
purple_certificate_register_scheme(PurpleCertificateScheme *scheme);

/** Unregister a CertificateScheme from libpurple
 *
 * @scheme:    Scheme to unregister.
 *                  If the scheme is not registered, this is a no-op.
 *
 * Returns: TRUE if the unregister completed successfully
 */
gboolean
purple_certificate_unregister_scheme(PurpleCertificateScheme *scheme);

/** Look up a registered PurpleCertificateVerifier by scheme and name
 * @scheme_name:  Scheme name. Case insensitive.
 * @ver_name:     The verifier name. Case insensitive.
 * Returns: Pointer to the located Verifier, or NULL if it isn't found.
 */
PurpleCertificateVerifier *
purple_certificate_find_verifier(const gchar *scheme_name, const gchar *ver_name);

/**
 * Get the list of registered CertificateVerifiers
 *
 * Returns: GList of all registered PurpleCertificateVerifier. This value
 *         is owned by libpurple
 */
GList *
purple_certificate_get_verifiers(void);

/**
 * Register a CertificateVerifier with libpurple
 *
 * @vr:     Verifier to register.
 * Returns: TRUE if register succeeded, otherwise FALSE
 */
gboolean
purple_certificate_register_verifier(PurpleCertificateVerifier *vr);

/**
 * Unregister a CertificateVerifier with libpurple
 *
 * @vr:     Verifier to unregister.
 * Returns: TRUE if unregister succeeded, otherwise FALSE
 */
gboolean
purple_certificate_unregister_verifier(PurpleCertificateVerifier *vr);

/** Look up a registered PurpleCertificatePool by scheme and name
 * @scheme_name:  Scheme name. Case insensitive.
 * @pool_name:    Pool name. Case insensitive.
 * Returns: Pointer to the located Pool, or NULL if it isn't found.
 */
PurpleCertificatePool *
purple_certificate_find_pool(const gchar *scheme_name, const gchar *pool_name);

/**
 * Get the list of registered Pools
 *
 * Returns: GList of all registered PurpleCertificatePool s. This value
 *         is owned by libpurple
 */
GList *
purple_certificate_get_pools(void);

/**
 * Register a CertificatePool with libpurple and call its init function
 *
 * @pool:   Pool to register.
 * Returns: TRUE if the register succeeded, otherwise FALSE
 */
gboolean
purple_certificate_register_pool(PurpleCertificatePool *pool);

/**
 * Unregister a CertificatePool with libpurple and call its uninit function
 *
 * @pool:   Pool to unregister.
 * Returns: TRUE if the unregister succeeded, otherwise FALSE
 */
gboolean
purple_certificate_unregister_pool(PurpleCertificatePool *pool);

/*@}*/


/**
 * Add a search path for certificates.
 *
 * @path:   Path to search for certificates.
 */
void purple_certificate_add_ca_search_path(const char *path);

G_END_DECLS

#endif /* _PURPLE_CERTIFICATE_H */
