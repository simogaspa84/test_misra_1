

#include <stdint.h>
#include <math.h>

#include "main.h"

#include "analog.h"
#include "machine.h"
#include "version.h"

#define ADC_NUM_SCAN                   3   /* numero canali ADc */
#define ADC_SAMPLES                    20  /* numero di campioni per singolo canale */
#define ADC_DMA_SAMPLES               (ADC_SAMPLES*ADC_NUM_SCAN)

#define ADC_I_OUT                      0
#define ADC_NTC_A                      1
#define ADC_NTC_B                      2
#define NUMBER_OF_ANALOG_INPUTS		   3

/* costanti di conversione */
#define VCC_ADC                        3300 /* mV */
#define ADC_MAX                        4095
#define I_OUT_K                        (float)16.5
#define R_VALUE                        2200
#define RTD_B                          3988
#define RTD_R_R                        10000
#define RTD_T_R                        (double)298.15
#define K2C                            (double)273.15

extern ADC_HandleTypeDef hadc1;

static volatile uint8_t adc1_complete;
static uint32_t adc_error;
static uint8_t adc_start_acq;
static uint16_t adc1_samples[ADC_DMA_SAMPLES+1];  /* buffer per l'acquisizone dei campioni dal ADC */


static uint32_t Adc2I(uint32_t adc)
{
	double calc;

	calc = adc;
	calc = calc/ADC_MAX;
	calc = calc/I_OUT_K;

	return (calc*100); /* cA */
}


static uint32_t Adc2T(uint32_t adc)
{
	double rt, t, conv;

	rt = adc;
	rt = (ADC_MAX/rt - 1)*R_VALUE;

	conv = log(rt/RTD_R_R)/log(M_E); /* da log a ln */
	t = (1/((conv/RTD_B)+(1/RTD_T_R)))-K2C;

	return t;
}


void AnalogInit(void)
{
	adc1_complete = 0;
	adc_error = 0;
	adc_start_acq = 1;
}


void AnalogManager(machine_status *machine)
{
	if (adc1_complete == 1) { /* elaborazione dati */
		uint16_t i,n;
		uint32_t avg_i, avg_ntc_a, avg_ntc_b;

		adc1_complete = 0;
		adc_start_acq = 1;
		HAL_ADC_Stop_DMA(&hadc1);

		/* medie */
		avg_i = avg_ntc_a = avg_ntc_b = 0;
		n = 0;
		for (i=0; i!=ADC_SAMPLES; i++) {
			avg_i += adc1_samples[n+ADC_I_OUT];
			avg_ntc_a += adc1_samples[n+ADC_NTC_A];
			avg_ntc_b += adc1_samples[n+ADC_NTC_B];
			n += NUMBER_OF_ANALOG_INPUTS;
		}
		avg_i /= ADC_SAMPLES;
		avg_ntc_a /= ADC_SAMPLES;
		avg_ntc_b /= ADC_SAMPLES;

		/* calcolo corrente */
		machine->i = Adc2I(avg_i);

		/* calcolo temperatura */
		machine->t_a = Adc2T(avg_ntc_a);
		machine->t_b = Adc2T(avg_ntc_b);
	}

	if (adc_start_acq) { /* avvio altra acquisizione */
		if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc1_samples, ADC_DMA_SAMPLES) == HAL_OK) {
			adc_start_acq = 0;
		}
	}
}


void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
	if (hadc->Instance == ADC1) {
		adc1_complete = 1;
	}
}


void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc) {
	adc_error++;
}

